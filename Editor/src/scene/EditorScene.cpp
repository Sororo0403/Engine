#include "scene/EditorScene.h"

#include "core/EngineContext.h"
#include "core/Logger.h"
#include "graphics/model/ModelManager.h"
#include "graphics/render/RenderTexture.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "input/InputSystem.h"
#include "input/Key.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"

#include <array>
#include <cmath>
#include <exception>
#include <format>
#include <imgui.h>
#include <utility>

EditorScene::EditorScene(const EngineContext &context)
    : ui_(context.ui), input_(context.input), models_(context.models),
      renderer_(document_.GetScene(), context.models, context.render),
      sceneViewPanel_(context.render) {}

EditorScene::~EditorScene() = default;

bool EditorScene::Initialize() {
	if (!ui_.EnableDocking()) {
		return false;
	}

	if (!renderer_.Initialize()) {
		return false;
	}

	gizmo_.Cancel();
	gizmoEditActive_ = false;

	cameraController_.Reset();

	stage_ = DialogStage::IDLE;
	pendingAction_ = EditorAction::NONE;
	exitQueued_ = false;
	closeApproved_ = false;
	errorOpenRequested_ = false;
	errorMessage_.clear();

	document_.New();
	selectedObjectId_ = 0;

	assetsPanel_.Clear();

	// 検索失敗時もEditorは起動し、Assetsパネルにエラーを表示する
	assets_.Refresh();

	Logger::Info("EditorSceneを初期化しました。");
	return true;
}

bool EditorScene::Update(float deltaTime) {
	if (!std::isfinite(deltaTime) || deltaTime < 0.0f) {
		Logger::Error("EditorScene::Update: 経過時間が無効です。");
		return false;
	}

	gizmo_.BeginFrame();

	if (exitQueued_ && stage_ == DialogStage::IDLE) {
		exitQueued_ = false;
		StartAction(EditorAction::EXIT);
	}

	EditorAction action = DrawMenuBar();

	dockLayout_.Draw(ui_, resetDockLayout_);
	resetDockLayout_ = false;

	const bool wasBusy = IsBusy();

	Scene &scene = document_.GetScene();
	EditorHistory &history = document_.GetHistory();

	ui_.BeginDisabled(wasBusy);

	const SceneObjectId previousSelection = selectedObjectId_;

	const auto hierarchyRequest =
	    hierarchyPanel_.Draw(ui_, scene, selectedObjectId_);

	if (previousSelection != selectedObjectId_) {
		FinishEdit();
	}

	inspectorPanel_.Draw(ui_, scene, selectedObjectId_, history);

	const auto requestedModelPath = assetsPanel_.Draw(ui_, assets_);

	const bool sceneViewReady = sceneViewPanel_.DrawUI(ui_, gizmo_);

	ui_.EndDisabled();

	if (!sceneViewReady) {
		return false;
	}

	if (!gizmoEditActive_ && !ImGui::IsAnyItemActive()) {
		history.CommitEdit();
	}

	if (!wasBusy) {
		if (hierarchyRequest.action != HierarchyPanel::Action::NONE) {
			// 編集中の内容を、物体を消す前に確定する
			FinishEdit();

			try {
				switch (hierarchyRequest.action) {
				case HierarchyPanel::Action::CREATE_EMPTY:
					selectedObjectId_ = history.CreateObject();
					break;

				case HierarchyPanel::Action::DELETE_OBJECT:
					if (history.DeleteObject(hierarchyRequest.objectId)) {
						if (selectedObjectId_ == hierarchyRequest.objectId) {
							selectedObjectId_ = 0;
						}
					} else {
						ShowError("Could not delete the object.");
					}
					break;

				case HierarchyPanel::Action::NONE:
					break;
				}
			} catch (const std::exception &error) {
				Logger::Error(std::format(
				    "EditorScene: 物体の編集に失敗しました: {}", error.what()));

				ShowError(
				    "Could not edit the object. See the log for details.");
			}
		}

		if (!IsBusy() && !requestedModelPath.empty()) {
			PlaceModel(requestedModelPath);
		}

		if (!IsBusy()) {
			if (action == EditorAction::NONE) {
				const bool objectPanelFocused =
				    hierarchyRequest.windowFocused ||
				    sceneViewPanel_.GetInput().windowFocused;

				action = ReadShortcut(objectPanelFocused);
			}

			if (action != EditorAction::NONE) {
				StartAction(action);
			}
		}
	}

	UpdateDialogs();

	// Undoで選択物体が削除された場合も、選択を解除する
	if (!scene.Contains(selectedObjectId_)) {
		selectedObjectId_ = 0;
	}

	const bool inputBlocked =
	    wasBusy || IsBusy() || action != EditorAction::NONE ||
	    !requestedModelPath.empty() ||
	    hierarchyRequest.action != HierarchyPanel::Action::NONE;

	ViewportInput viewport = sceneViewPanel_.GetInput();

	if (inputBlocked) {
		viewport = {};
	}

	ViewportInput cameraInput = viewport;

	if (gizmo_.IsUsing()) {
		cameraInput = {};
	}

	cameraController_.Update(input_, cameraInput, deltaTime);

	auto *target = sceneViewPanel_.GetRenderTexture();

	if (!target) {
		if (gizmoEditActive_) {
			history.CommitEdit();
			gizmoEditActive_ = false;
		}

		gizmo_.Cancel();
		return true;
	}

	if (!renderer_.SetCameraPose(cameraController_.GetPose())) {
		Logger::Error("EditorScene: カメラの姿勢が無効です。");
		return false;
	}

	if (!renderer_.Update(target->GetWidth(), target->GetHeight())) {
		return false;
	}

	DirectX::XMFLOAT4X4 view;
	DirectX::XMFLOAT4X4 projection;

	if (!renderer_.GetCameraMatrices(view, projection)) {
		Logger::Error("EditorScene: カメラ行列を取得できませんでした。");
		return false;
	}

	if (!inputBlocked &&
	    ReadFrameSelectedShortcut(viewport, hierarchyRequest.windowFocused) &&
	    FrameSelected(projection)) {
		// 移動後のカメラを、ギズモや選択判定にも同じフレームで反映する
		if (!renderer_.SetCameraPose(cameraController_.GetPose())) {
			Logger::Error("EditorScene: 移動後のカメラの姿勢が無効です。");
			return false;
		}

		if (!renderer_.Update(target->GetWidth(), target->GetHeight())) {
			return false;
		}

		if (!renderer_.GetCameraMatrices(view, projection)) {
			Logger::Error(
			    "EditorScene: 移動後のカメラ行列を取得できませんでした。");
			return false;
		}
	}

	SceneObject *selected = scene.FindObject(selectedObjectId_);

	Transform *transform = nullptr;

	if (selected && !selected->modelPath.empty()) {
		transform = &selected->transform;
	}

	const Transform beforeGizmo = transform ? *transform : Transform{};

	const bool gizmoEnabled = !inputBlocked && viewport.applicationFocused &&
	                          !viewport.rightDown &&
	                          (!viewport.widgetActive || gizmo_.IsUsing());

	const auto gizmoResult =
	    sceneViewPanel_.DrawGizmo(ui_, gizmo_, view, projection,
	                              selectedObjectId_, transform, gizmoEnabled);

	if (gizmoResult.changed) {
		if (!gizmoEditActive_) {
			if (!history.BeginTransform(selectedObjectId_, beforeGizmo)) {
				Logger::Error(
				    "EditorScene: ギズモの編集履歴を開始できませんでした。");
				return false;
			}

			gizmoEditActive_ = true;
		}

		if (!renderer_.Update(target->GetWidth(), target->GetHeight())) {
			return false;
		}
	}

	if (gizmoEditActive_ && !gizmo_.IsUsing()) {
		history.CommitEdit();
		gizmoEditActive_ = false;
	}

	if (!gizmoResult.blocksSelection && viewport.imageHovered &&
	    viewport.imagePositionValid && viewport.applicationFocused &&
	    !viewport.widgetActive && viewport.leftPressed && !viewport.rightDown) {
		const SceneObjectId picked =
		    renderer_.Pick(viewport.imageX, viewport.imageY);

		if (picked != selectedObjectId_) {
			FinishEdit();
			selectedObjectId_ = picked;
		}
	}

	// ギズモによる再更新とクリック選択が終わってから登録する
	if (!sceneGrid_.Update(renderer_)) {
		Logger::Error("EditorScene: グリッドを登録できませんでした。");
		return false;
	}

	if (!selectionBounds_.Update(renderer_, selectedObjectId_)) {
		Logger::Error(
		    "EditorScene: 選択物体の外接ボックスを登録できませんでした。");
		return false;
	}

	// 全ての線を登録した後、一度だけ頂点バッファへ転送する
	if (!renderer_.UpdateDebugLines()) {
		Logger::Error("EditorScene: 線の頂点バッファを更新できませんでした。");
		return false;
	}

	return true;
}

void EditorScene::Draw() const {
	auto *target = sceneViewPanel_.GetRenderTexture();

	if (target) {
		renderer_.Draw(target);
	}
}

void EditorScene::Finalize() {
	FinishEdit();

	sceneViewPanel_.Finalize();
	renderer_.Finalize();

	document_.New();
	selectedObjectId_ = 0;

	stage_ = DialogStage::IDLE;
	pendingAction_ = EditorAction::NONE;
	exitQueued_ = false;
	closeApproved_ = false;
	errorOpenRequested_ = false;
	errorMessage_.clear();

	assetsPanel_.Clear();
	assets_.Clear();

	cameraController_.Reset();
}

bool EditorScene::RequestClose() {
	// ほかのダイアログ中の終了要求は、操作完了まで保持する
	if (!closeApproved_ && pendingAction_ != EditorAction::EXIT) {
		exitQueued_ = true;
	}

	return false;
}

bool EditorScene::IsCloseApproved() const { return closeApproved_; }

std::string EditorScene::GetWindowTitle() const {
	const std::string name =
	    document_.GetFilePath().empty()
	        ? "Untitled"
	        : PathUtils::ToUtf8(document_.GetFilePath().filename());

	return std::format("{}{} - Editor", name,
	                   document_.IsModified() ? "*" : "");
}

EditorScene::EditorAction EditorScene::DrawMenuBar() {
	EditorAction action = EditorAction::NONE;

	if (!ui_.BeginMainMenuBar()) {
		return action;
	}

	if (ui_.BeginMenu("File")) {
		const bool enabled = !IsBusy();

		if (ui_.MenuItem("New", "Ctrl+N", enabled)) {
			action = EditorAction::NEW;
		}

		if (ui_.MenuItem("Open...", "Ctrl+O", enabled)) {
			action = EditorAction::OPEN;
		}

		ui_.Separator();

		if (ui_.MenuItem("Save", "Ctrl+S", enabled)) {
			action = EditorAction::SAVE;
		}

		if (ui_.MenuItem("Save As...", "Ctrl+Shift+S", enabled)) {
			action = EditorAction::SAVE_AS;
		}

		ui_.Separator();

		if (ui_.MenuItem("Exit", nullptr, enabled)) {
			action = EditorAction::EXIT;
		}

		ui_.EndMenu();
	}

	if (ui_.BeginMenu("Edit")) {
		const EditorHistory &history = document_.GetHistory();

		const bool enabled =
		    !IsBusy() && !history.IsEditing() && !gizmo_.IsUsing();

		if (ui_.MenuItem("Undo", "Ctrl+Z", enabled && history.CanUndo())) {
			action = EditorAction::UNDO;
		}

		if (ui_.MenuItem("Redo", "Ctrl+Y", enabled && history.CanRedo())) {
			action = EditorAction::REDO;
		}

		ui_.Separator();

		const bool canDuplicate =
		    enabled && document_.GetScene().Contains(selectedObjectId_);

		if (ui_.MenuItem("Duplicate", "Ctrl+D", canDuplicate)) {
			action = EditorAction::DUPLICATE_OBJECT;
		}

		ui_.EndMenu();
	}

	if (ui_.BeginMenu("View")) {
		if (ui_.MenuItem("Reset Layout", nullptr, !IsBusy())) {
			resetDockLayout_ = true;
		}

		ui_.EndMenu();
	}

	ui_.EndMainMenuBar();
	return action;
}

EditorScene::EditorAction
EditorScene::ReadShortcut(bool objectPanelFocused) const {
	// 文字入力中のCtrl+Zは、ImGuiの入力欄へ任せる
	if (IsBusy() || ui_.IsTextInputActive() || gizmo_.IsUsing() ||
	    document_.GetHistory().IsEditing()) {
		return EditorAction::NONE;
	}

	const Keyboard &keyboard = input_.GetKeyboard();

	if (!keyboard.IsDown(Key::CTRL)) {
		return EditorAction::NONE;
	}

	if (keyboard.IsPressed(Key::D) && !keyboard.IsDown(Key::SHIFT) &&
	    !keyboard.IsDown(Key::ALT) && objectPanelFocused &&
	    !ImGui::IsAnyItemActive() &&
	    !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
	    document_.GetScene().Contains(selectedObjectId_)) {
		return EditorAction::DUPLICATE_OBJECT;
	}

	if (keyboard.IsPressed(Key::Z)) {
		return keyboard.IsDown(Key::SHIFT) ? EditorAction::REDO
		                                   : EditorAction::UNDO;
	}

	if (keyboard.IsPressed(Key::Y)) {
		return EditorAction::REDO;
	}

	if (keyboard.IsPressed(Key::S)) {
		return keyboard.IsDown(Key::SHIFT) ? EditorAction::SAVE_AS
		                                   : EditorAction::SAVE;
	}

	if (keyboard.IsPressed(Key::O)) {
		return EditorAction::OPEN;
	}

	if (keyboard.IsPressed(Key::N)) {
		return EditorAction::NEW;
	}

	return EditorAction::NONE;
}

void EditorScene::StartAction(EditorAction action) {
	if (stage_ != DialogStage::IDLE || closeApproved_ ||
	    action == EditorAction::NONE) {
		return;
	}

	// 保存やUndoを実行する前に、編集中の値を確定する
	FinishEdit();

	switch (action) {
	case EditorAction::UNDO:
		if (document_.GetHistory().CanUndo() &&
		    !document_.GetHistory().Undo()) {
			ShowError("Could not undo the edit.");
		}
		return;

	case EditorAction::REDO:
		if (document_.GetHistory().CanRedo() &&
		    !document_.GetHistory().Redo()) {
			ShowError("Could not redo the edit.");
		}
		return;

	case EditorAction::NEW:
	case EditorAction::OPEN:
	case EditorAction::EXIT:
		// 保存確認が終わるまで、実際の操作は保留する
		pendingAction_ = action;

		if (document_.IsModified()) {
			const std::string name =
			    document_.GetFilePath().empty()
			        ? "Untitled"
			        : PathUtils::ToUtf8(document_.GetFilePath().filename());

			saveChangesDialog_.Show(name);
			stage_ = DialogStage::CONFIRM_SAVE;
		} else {
			ExecutePendingAction();
		}
		return;

	case EditorAction::SAVE:
		pendingAction_ = EditorAction::NONE;
		StartSave(false);
		return;

	case EditorAction::SAVE_AS:
		pendingAction_ = EditorAction::NONE;
		StartSave(true);
		return;
	case EditorAction::DUPLICATE_OBJECT: {
		try {
			const SceneObjectId duplicatedId =
			    document_.GetHistory().DuplicateObject(selectedObjectId_);

			if (duplicatedId == 0) {
				ShowError("Could not duplicate the object.");
				return;
			}

			selectedObjectId_ = duplicatedId;
		} catch (const std::exception &error) {
			Logger::Error(std::format(
			    "EditorScene: 物体の複製に失敗しました: {}", error.what()));

			ShowError(
			    "Could not duplicate the object. See the log for details.");
		}
		return;
	}
	case EditorAction::NONE:
		return;
	}
}

void EditorScene::ExecutePendingAction() {
	const EditorAction action = pendingAction_;

	pendingAction_ = EditorAction::NONE;
	stage_ = DialogStage::IDLE;

	switch (action) {
	case EditorAction::NEW:
		document_.New();

		gizmo_.Cancel();
		gizmoEditActive_ = false;

		selectedObjectId_ = 0;
		cameraController_.Reset();
		return;

	case EditorAction::OPEN:
		fileDialog_.Show(SceneFileDialogMode::OPEN, document_.GetFilePath());

		stage_ = DialogStage::OPEN_FILE;
		return;

	case EditorAction::EXIT:
		closeApproved_ = true;
		return;

	default:
		return;
	}
}

void EditorScene::StartSave(bool saveAs) {
	if (!saveAs && !document_.GetFilePath().empty()) {
		if (document_.Save()) {
			ExecutePendingAction();
		} else {
			ShowError("Could not save the scene. See the log for details.");
		}

		return;
	}

	fileDialog_.Show(SceneFileDialogMode::SAVE, document_.GetFilePath());

	stage_ = DialogStage::SAVE_FILE;
}

void EditorScene::UpdateDialogs() {
	switch (stage_) {
	case DialogStage::IDLE:
		return;

	case DialogStage::CONFIRM_SAVE: {
		const auto result = saveChangesDialog_.Draw(ui_);

		switch (result) {
		case SaveChangesResult::SAVE:
			StartSave(false);
			break;

		case SaveChangesResult::DISCARD:
			ExecutePendingAction();
			break;

		case SaveChangesResult::CANCEL:
			pendingAction_ = EditorAction::NONE;
			stage_ = DialogStage::IDLE;
			break;

		case SaveChangesResult::NONE:
			break;
		}

		return;
	}

	case DialogStage::OPEN_FILE: {
		const auto result = fileDialog_.Draw(ui_);

		if (result == SceneFileDialogResult::CANCELLED) {
			stage_ = DialogStage::IDLE;
			return;
		}

		if (result == SceneFileDialogResult::ACCEPTED) {
			if (document_.Open(fileDialog_.GetSelectedPath())) {
				gizmo_.Cancel();
				gizmoEditActive_ = false;

				selectedObjectId_ = 0;
				cameraController_.Reset();
				stage_ = DialogStage::IDLE;
			} else {
				ShowError("Could not open the scene. See the log for details.");
			}
		}

		return;
	}

	case DialogStage::SAVE_FILE: {
		const auto result = fileDialog_.Draw(ui_);

		if (result == SceneFileDialogResult::CANCELLED) {
			// 保存先選択を取り消した場合は、NewやExitも取り消す
			pendingAction_ = EditorAction::NONE;
			stage_ = DialogStage::IDLE;
			return;
		}

		if (result == SceneFileDialogResult::ACCEPTED) {
			if (document_.SaveAs(fileDialog_.GetSelectedPath())) {
				ExecutePendingAction();
			} else {
				ShowError("Could not save the scene. See the log for details.");
			}
		}

		return;
	}

	case DialogStage::ERROR: {
		constexpr const char *kTitle =
		    "File operation failed###SceneOperationError";

		if (errorOpenRequested_) {
			ui_.OpenPopup(kTitle);
			errorOpenRequested_ = false;
		}

		if (ui_.BeginPopupModal(kTitle)) {
			ui_.Text(errorMessage_);

			if (ui_.Button("OK")) {
				ui_.CloseCurrentPopup();
				stage_ = DialogStage::IDLE;
			}

			ui_.EndPopup();
		}

		return;
	}
	}
}

void EditorScene::ShowError(std::string message) {
	errorMessage_ = std::move(message);
	errorOpenRequested_ = true;

	pendingAction_ = EditorAction::NONE;
	stage_ = DialogStage::ERROR;
}

bool EditorScene::IsBusy() const {
	return stage_ != DialogStage::IDLE || exitQueued_ || closeApproved_;
}

void EditorScene::PlaceModel(const std::filesystem::path &modelPath) {
	try {
		if (modelPath.empty()) {
			return;
		}

		FinishEdit();

		const std::string name = PathUtils::ToUtf8(modelPath.stem());

		// 読み込みを確認してから、シーンと履歴へ追加する
		const auto model = models_.Load(modelPath);

		if (!model) {
			ShowError("Could not load the model. See the log for details.");
			return;
		}

		selectedObjectId_ =
		    document_.GetHistory().CreateObject(name, modelPath);
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene::PlaceModel: {}", error.what()));

		ShowError("Could not place the model. See the log for details.");
	}
}

void EditorScene::FinishEdit() {
	document_.GetHistory().CommitEdit();

	gizmo_.Cancel();
	gizmoEditActive_ = false;
}

bool EditorScene::ReadFrameSelectedShortcut(const ViewportInput &viewport,
                                            bool hierarchyFocused) const {
	if (IsBusy() || selectedObjectId_ == 0 || !viewport.applicationFocused ||
	    (!viewport.windowFocused && !hierarchyFocused) ||
	    viewport.widgetActive || viewport.rightDown ||
	    ui_.IsTextInputActive() || ImGui::IsAnyItemActive() ||
	    gizmo_.IsUsing() || document_.GetHistory().IsEditing()) {
		return false;
	}

	const Keyboard &keyboard = input_.GetKeyboard();

	// 修飾キー付きの操作とは区別する
	if (keyboard.IsDown(Key::CTRL) || keyboard.IsDown(Key::ALT) ||
	    keyboard.IsDown(Key::SHIFT)) {
		return false;
	}

	return keyboard.IsPressed(Key::F);
}

bool EditorScene::FrameSelected(const DirectX::XMFLOAT4X4 &projection) {
	std::array<DirectX::XMFLOAT3, 8> corners{};

	// 未選択や、モデルを持たない空の物体では移動しない
	if (!renderer_.GetObjectBounds(selectedObjectId_, corners)) {
		return false;
	}

	if (!cameraController_.FrameBounds(corners, projection)) {
		Logger::Warning(
		    "EditorScene: 選択モデルを現在の描画範囲に収められませんでした。");
		return false;
	}

	return true;
}
