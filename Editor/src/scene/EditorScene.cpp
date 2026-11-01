#include "scene/EditorScene.h"

#include "core/EngineContext.h"
#include "core/Logger.h"
#include "graphics/model/ModelManager.h"
#include "graphics/render/RenderTexture.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "input/InputSystem.h"
#include "input/Key.h"
#include "math/TransformUtils.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include "selection/SelectionFrustum.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <format>
#include <imgui.h>
#include <utility>
#include <vector>

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
	selection_.Clear();

	hierarchyPanel_.Clear();
	assetsPanel_.Clear();

	// 検索失敗時もEditorは起動し、Assetsパネルにエラーを表示する
	assets_.Refresh();
	clipboard_.Clear();

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

	const auto hierarchyRequest = hierarchyPanel_.Draw(ui_, scene, selection_);

	if (hierarchyRequest.selectionChanged) {
		FinishEdit();
	}

	// ギズモ操作とInspectorの編集を、同じ履歴として混在させない
	ui_.BeginDisabled(gizmo_.IsUsing() || gizmoEditActive_ ||
	                  sceneViewPanel_.IsSelecting());
	inspectorPanel_.Draw(ui_, scene, selection_.GetActiveId(), history);
	ui_.EndDisabled();

	const auto requestedModelPath = assetsPanel_.Draw(ui_, assets_);

	const auto toolbarMode = selection_.GetCount() > 1
	                             ? TransformGizmo::SelectionMode::MULTIPLE
	                             : TransformGizmo::SelectionMode::SINGLE;
	const bool sceneViewReady = sceneViewPanel_.DrawUI(ui_, gizmo_, toolbarMode);
	if (hierarchyRequest.windowFocused) {
		hierarchySelectionScope_ = true;
	} else if (sceneViewPanel_.GetInput().windowFocused) {
		hierarchySelectionScope_ = false;
	}

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
					selection_.SelectOnly(history.CreateObject());
					break;
				case HierarchyPanel::Action::DELETE_OBJECTS:
					DeleteSelectedObjects();
					break;
				case HierarchyPanel::Action::COPY_OBJECTS:
					CopySelectedObjects(false);
					break;
				case HierarchyPanel::Action::CUT_OBJECTS:
					CopySelectedObjects(true);
					break;
				case HierarchyPanel::Action::DUPLICATE_OBJECTS:
					DuplicateSelectedObjects();
					break;
				case HierarchyPanel::Action::RESET_TRANSFORMS:
					ResetSelectedTransforms();
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

	// 消えた物体だけを除き、残っている選択は維持する
	if (selection_.RemoveMissing(scene)) {
		FinishEdit();
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

	if (gizmo_.IsUsing() || sceneViewPanel_.IsSelecting()) {
		cameraInput = {};
	}

	cameraController_.Update(input_, cameraInput, deltaTime);

	auto *target = sceneViewPanel_.GetRenderTexture();

	if (!target) {
		sceneViewPanel_.CancelSelection();
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

	FrameRequest frameRequest = frameRequest_;
	frameRequest_ = FrameRequest::NONE;
	if (!inputBlocked && frameRequest == FrameRequest::NONE) {
		frameRequest = ReadFrameShortcut(viewport, hierarchyRequest.windowFocused);
	}
	if (frameRequest != FrameRequest::NONE && FrameObjects(frameRequest, projection)) {
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

	const SceneObjectId activeId = selection_.GetActiveId();
	const bool multipleSelection = selection_.GetCount() > 1;
	const auto selectionMode = multipleSelection
	                               ? TransformGizmo::SelectionMode::MULTIPLE
	                               : TransformGizmo::SelectionMode::SINGLE;

	SceneObject *selected = scene.FindObject(activeId);

	Transform *transform = nullptr;

	if (multipleSelection) {
		if (gizmoEditActive_ ||
		    MakeSelectionTransform(selectionGizmoTransform_)) {
			transform = &selectionGizmoTransform_;
		}
	} else if (HasSingleSelection() && selected &&
	           !selected->modelPath.empty()) {
		transform = &selected->transform;
	}

	const Transform beforeGizmo = transform ? *transform : Transform{};

	const bool gizmoEnabled = !inputBlocked && transform != nullptr &&
	                          viewport.applicationFocused &&
	                          !viewport.rightDown &&
	                          (!viewport.widgetActive || gizmo_.IsUsing());

	const auto gizmoResult = sceneViewPanel_.DrawGizmo(
	    ui_, gizmo_, view, projection, activeId, transform, gizmoEnabled,
	    selectionMode);

	if (gizmoResult.changed) {
		if (!gizmoEditActive_) {
			if (multipleSelection) {
				// ギズモ用の値だけが変わった段階で、全物体の開始値を保持する
				if (!history.BeginTransforms(selection_.GetIds())) {
					Logger::Error(
					    "EditorScene: 複数物体の編集履歴を開始できませんでした。");
					return false;
				}
				selectionGizmoStart_ = beforeGizmo;
			} else if (!history.BeginTransform(activeId, beforeGizmo)) {
				Logger::Error(
				    "EditorScene: ギズモの編集履歴を開始できませんでした。");
				return false;
			}

			gizmoEditActive_ = true;
		}

		bool applied = true;
		if (multipleSelection) {
			applied = history.UpdateTransforms(
			    selectionGizmoStart_, selectionGizmoTransform_);
			if (!applied) {
				// 無効な変形は反映せず、直前の有効な状態で編集を確定する
				selectionGizmoTransform_ = beforeGizmo;
				FinishEdit();
				Logger::Error("EditorScene: 複数物体の変形情報が無効です。");
			}
		}

		if (applied &&
		    !renderer_.Update(target->GetWidth(), target->GetHeight())) {
			return false;
		}
	}

	if (gizmoEditActive_ && !gizmo_.IsUsing()) {
		history.CommitEdit();
		gizmoEditActive_ = false;
	}

	const auto selectionRequest = sceneViewPanel_.UpdateSelection(
	    ui_, !inputBlocked, gizmoResult.blocksSelection);
	if (selectionRequest.action != SceneViewPanel::SelectionRequest::Action::NONE) {
		try {
			if (ApplySceneSelection(selectionRequest, view, projection)) {
				FinishEdit();
			}
		} catch (const std::exception &error) {
			Logger::Error(std::format("EditorScene: 物体の選択に失敗しました: {}",
			                          error.what()));
			ShowError("Could not select objects. See the log for details.");
		}
	}

	// ギズモによる再更新とクリック選択が終わってから登録する
	if (showGrid_ && !sceneGrid_.Update(renderer_)) {
		Logger::Error("EditorScene: グリッドを登録できませんでした。");
		return false;
	}

	for (const SceneObjectId id : selection_.GetIds()) {
		if (showSelectionBounds_ && !selectionBounds_.Update(renderer_, id)) {
			Logger::Error(
			    "EditorScene: 選択物体の外接ボックスを登録できませんでした。");
			return false;
		}
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
	selection_.Clear();

	hierarchyPanel_.Clear();

	stage_ = DialogStage::IDLE;
	pendingAction_ = EditorAction::NONE;
	exitQueued_ = false;
	closeApproved_ = false;
	errorOpenRequested_ = false;
	errorMessage_.clear();

	assetsPanel_.Clear();
	assets_.Clear();
	clipboard_.Clear();

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
		    !IsBusy() && !history.IsEditing() && !gizmo_.IsUsing() &&
		    !sceneViewPanel_.IsSelecting();

		if (ui_.MenuItem("Undo", "Ctrl+Z", enabled && history.CanUndo())) {
			action = EditorAction::UNDO;
		}

		if (ui_.MenuItem("Redo", "Ctrl+Y", enabled && history.CanRedo())) {
			action = EditorAction::REDO;
		}

		ui_.Separator();

		const bool objectEnabled =
		    enabled && !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
		    HasSelection();
		if (ui_.MenuItem("Select All", "Ctrl+A", enabled)) {
			action = EditorAction::SELECT_ALL;
		}
		if (ui_.MenuItem("Deselect All", "Esc", enabled && HasSelection())) {
			action = EditorAction::DESELECT_ALL;
		}
		ui_.Separator();
		if (ui_.MenuItem("Copy", "Ctrl+C", objectEnabled)) {
			action = EditorAction::COPY_OBJECTS;
		}
		if (ui_.MenuItem("Cut", "Ctrl+X", objectEnabled)) {
			action = EditorAction::CUT_OBJECTS;
		}
		if (ui_.MenuItem("Paste", "Ctrl+V", enabled && clipboard_.HasObjects())) {
			action = EditorAction::PASTE_OBJECTS;
		}
		ui_.Separator();

		if (ui_.MenuItem("Duplicate", "Ctrl+D", objectEnabled)) {
			action = EditorAction::DUPLICATE_OBJECTS;
		}

		if (ui_.MenuItem("Delete", "Delete", objectEnabled)) {
			action = EditorAction::DELETE_OBJECTS;
		}
		if (ui_.MenuItem("Reset Transform", nullptr, objectEnabled)) {
			action = EditorAction::RESET_TRANSFORMS;
		}

		ui_.EndMenu();
	}

	if (ui_.BeginMenu("View")) {
		const bool enabled = !IsBusy() && !document_.GetHistory().IsEditing() &&
		                     !gizmo_.IsUsing() && !sceneViewPanel_.IsSelecting();
		if (ui_.MenuItem("Frame Selected", "F", enabled && HasSelection())) {
			action = EditorAction::FRAME_SELECTED;
		}
		if (ui_.MenuItem("Frame All", "Shift+F", enabled)) {
			action = EditorAction::FRAME_ALL;
		}
		if (ui_.MenuItem("Reset Camera", nullptr, enabled)) {
			action = EditorAction::RESET_CAMERA;
		}
		ui_.Separator();
		ImGui::MenuItem("Grid", nullptr, &showGrid_, !IsBusy());
		ImGui::MenuItem("Selection Bounds", nullptr, &showSelectionBounds_, !IsBusy());
		ui_.Separator();
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
	// 文字入力中の操作は、ImGuiの入力欄へ任せる
	if (IsBusy() || ui_.IsTextInputActive() || gizmo_.IsUsing() ||
	    sceneViewPanel_.IsSelecting() ||
	    document_.GetHistory().IsEditing()) {
		return EditorAction::NONE;
	}

	const Keyboard &keyboard = input_.GetKeyboard();

	const bool control = keyboard.IsDown(Key::CTRL);
	const bool shift = keyboard.IsDown(Key::SHIFT);
	const bool alt = keyboard.IsDown(Key::ALT);

	const bool selectionEnabled =
	    objectPanelFocused && !ImGui::IsAnyItemActive() &&
	    !ImGui::IsMouseDown(ImGuiMouseButton_Right);
	const bool objectEnabled = selectionEnabled && HasSelection();
	if (objectEnabled && !control && !shift && !alt) {
		if (keyboard.IsPressed(Key::W)) {
			return EditorAction::GIZMO_MOVE;
		}
		if (keyboard.IsPressed(Key::E)) {
			return EditorAction::GIZMO_ROTATE;
		}
		if (keyboard.IsPressed(Key::R)) {
			return EditorAction::GIZMO_SCALE;
		}
	}
	if (selectionEnabled && !control && !shift && !alt &&
	    keyboard.IsPressed(Key::ESCAPE)) {
		return EditorAction::DESELECT_ALL;
	}
	if (selectionEnabled && control && !shift && !alt &&
	    keyboard.IsPressed(Key::A)) {
		return EditorAction::SELECT_ALL;
	}
	if (selectionEnabled && control && !shift && !alt) {
		if (objectEnabled && keyboard.IsPressed(Key::C)) {
			return EditorAction::COPY_OBJECTS;
		}
		if (objectEnabled && keyboard.IsPressed(Key::X)) {
			return EditorAction::CUT_OBJECTS;
		}
		if (clipboard_.HasObjects() && keyboard.IsPressed(Key::V)) {
			return EditorAction::PASTE_OBJECTS;
		}
	}

	if (objectEnabled && !control && !shift && !alt &&
	    keyboard.IsPressed(Key::DELETE_KEY)) {
		return EditorAction::DELETE_OBJECTS;
	}

	if (!control) {
		return EditorAction::NONE;
	}

	if (objectEnabled && !shift && !alt && keyboard.IsPressed(Key::D)) {
		return EditorAction::DUPLICATE_OBJECTS;
	}

	if (keyboard.IsPressed(Key::Z)) {
		return shift ? EditorAction::REDO : EditorAction::UNDO;
	}

	if (keyboard.IsPressed(Key::Y)) {
		return EditorAction::REDO;
	}

	if (keyboard.IsPressed(Key::S)) {
		return shift ? EditorAction::SAVE_AS : EditorAction::SAVE;
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
	case EditorAction::RESET_CAMERA:
		cameraController_.Reset();
		frameRequest_ = FrameRequest::NONE;
		return;
	case EditorAction::RESET_TRANSFORMS:
		ResetSelectedTransforms();
		return;
	case EditorAction::COPY_OBJECTS:
		CopySelectedObjects(false);
		return;
	case EditorAction::CUT_OBJECTS:
		CopySelectedObjects(true);
		return;
	case EditorAction::PASTE_OBJECTS:
		PasteObjects();
		return;
	case EditorAction::GIZMO_MOVE:
		gizmo_.SetOperation(TransformGizmo::Operation::TRANSLATE);
		return;
	case EditorAction::GIZMO_ROTATE:
		gizmo_.SetOperation(TransformGizmo::Operation::ROTATE);
		return;
	case EditorAction::GIZMO_SCALE:
		gizmo_.SetOperation(TransformGizmo::Operation::SCALE);
		return;
	case EditorAction::SELECT_ALL: {
		if (hierarchySelectionScope_) {
			hierarchyPanel_.SelectAllVisible(document_.GetScene(), selection_);
		} else {
			std::vector<SceneObjectId> ids;
			for (const auto &[id, object] : document_.GetScene().GetObjects()) {
				if (!object.modelPath.empty()) {
					ids.push_back(id);
				}
			}
			selection_.Set(std::move(ids));
		}
		return;
	}
	case EditorAction::DESELECT_ALL:
		selection_.Clear();
		return;
	case EditorAction::FRAME_SELECTED:
		frameRequest_ = FrameRequest::SELECTED;
		return;
	case EditorAction::FRAME_ALL:
		frameRequest_ = FrameRequest::ALL;
		return;
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
	case EditorAction::DUPLICATE_OBJECTS:
		DuplicateSelectedObjects();
		return;

	case EditorAction::DELETE_OBJECTS:
		DeleteSelectedObjects();
		return;
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

		selection_.Clear();
		cameraController_.Reset();
		hierarchyPanel_.Clear();

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

				selection_.Clear();
				cameraController_.Reset();
				hierarchyPanel_.Clear();

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

		const SceneObjectId createdId =
		    document_.GetHistory().CreateObject(name, modelPath);

		selection_.SelectOnly(createdId);
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene::PlaceModel: {}", error.what()));

		ShowError("Could not place the model. See the log for details.");
	}
}

void EditorScene::FinishEdit() {
	document_.GetHistory().CommitEdit();

	gizmo_.Cancel();
	gizmoEditActive_ = false;
	sceneViewPanel_.CancelSelection();
}

bool EditorScene::ApplySceneSelection(
    const SceneViewPanel::SelectionRequest &request,
    const DirectX::XMFLOAT4X4 &view,
    const DirectX::XMFLOAT4X4 &projection) {
	using Action = SceneViewPanel::SelectionRequest::Action;
	if (request.action == Action::CLICK) {
		const SceneObjectId picked =
		    renderer_.Pick(request.minimum.x, request.minimum.y);
		return request.additive ? selection_.Toggle(picked)
		                        : selection_.SelectOnly(picked);
	}
	if (request.action != Action::BOX) {
		return false;
	}
	SelectionFrustum volume;
	if (!volume.Initialize(view, projection, request.minimum, request.maximum)) {
		return false;
	}

	std::vector<SceneObjectId> ids;
	if (request.additive) {
		const auto current = selection_.GetIds();
		ids.assign(current.begin(), current.end());
	}
	for (const auto &[id, object] : document_.GetScene().GetObjects()) {
		if (object.modelPath.empty()) {
			continue;
		}
		std::array<DirectX::XMFLOAT3, 8> corners;
		if (renderer_.GetObjectBounds(id, corners) && volume.Intersects(corners) &&
		    std::find(ids.begin(), ids.end(), id) == ids.end()) {
			ids.push_back(id);
		}
	}
	const auto current = selection_.GetIds();
	if (std::equal(current.begin(), current.end(), ids.begin(), ids.end())) {
		return false;
	}
	return selection_.Set(std::move(ids));
}

bool EditorScene::MakeSelectionTransform(Transform &transform) const {
	if (selection_.GetCount() < 2) {
		return false;
	}

	const Scene &scene = document_.GetScene();
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
	for (const SceneObjectId id : selection_.GetIds()) {
		const SceneObject *object = scene.FindObject(id);
		if (!object || !TransformUtils::IsValid(object->transform)) {
			return false;
		}
		x += object->transform.translation.x;
		y += object->transform.translation.y;
		z += object->transform.translation.z;
	}

	const double count = static_cast<double>(selection_.GetCount());
	Transform result;
	result.translation = {
	    static_cast<float>(x / count),
	    static_cast<float>(y / count),
	    static_cast<float>(z / count),
	};
	if (!TransformUtils::IsValid(result)) {
		return false;
	}

	transform = result;
	return true;
}

EditorScene::FrameRequest EditorScene::ReadFrameShortcut(
    const ViewportInput &viewport, bool hierarchyFocused) const {
	if (IsBusy() || !viewport.applicationFocused ||
	    (!viewport.windowFocused && !hierarchyFocused) || viewport.widgetActive ||
	    viewport.rightDown || ui_.IsTextInputActive() || ImGui::IsAnyItemActive() ||
	    gizmo_.IsUsing() || sceneViewPanel_.IsSelecting() ||
	    document_.GetHistory().IsEditing()) {
		return FrameRequest::NONE;
	}
	const Keyboard &keyboard = input_.GetKeyboard();
	if (keyboard.IsDown(Key::CTRL) || keyboard.IsDown(Key::ALT) ||
	    !keyboard.IsPressed(Key::F)) {
		return FrameRequest::NONE;
	}
	return keyboard.IsDown(Key::SHIFT) ? FrameRequest::ALL
	                                 : FrameRequest::SELECTED;
}

bool EditorScene::FrameObjects(
    FrameRequest request, const DirectX::XMFLOAT4X4 &projection) {
	DirectX::XMFLOAT3 minimum{};
	DirectX::XMFLOAT3 maximum{};
	bool found = false;
	for (const auto &[id, object] : document_.GetScene().GetObjects()) {
		if (object.modelPath.empty() ||
		    (request == FrameRequest::SELECTED && !selection_.Contains(id))) {
			continue;
		}
		std::array<DirectX::XMFLOAT3, 8> corners;
		if (!renderer_.GetObjectBounds(id, corners)) {
			continue;
		}
		for (const auto &corner : corners) {
			if (!found) {
				minimum = maximum = corner;
				found = true;
			} else {
				minimum.x = (std::min)(minimum.x, corner.x);
				minimum.y = (std::min)(minimum.y, corner.y);
				minimum.z = (std::min)(minimum.z, corner.z);
				maximum.x = (std::max)(maximum.x, corner.x);
				maximum.y = (std::max)(maximum.y, corner.y);
				maximum.z = (std::max)(maximum.z, corner.z);
			}
		}
	}
	if (!found) {
		return false;
	}
	std::array<DirectX::XMFLOAT3, 8> corners;
	for (std::size_t i = 0; i < corners.size(); ++i) {
		corners[i] = {
		    (i & 1) ? maximum.x : minimum.x,
		    (i & 2) ? maximum.y : minimum.y,
		    (i & 4) ? maximum.z : minimum.z,
		};
	}
	if (!cameraController_.FrameBounds(corners, projection)) {
		Logger::Warning(
		    "EditorScene: 対象モデルを現在の描画範囲に収められませんでした。");
		return false;
	}
	return true;
}

void EditorScene::DuplicateSelectedObjects() {
	if (!HasSelection()) {
		return;
	}

	try {
		auto createdIds =
		    document_.GetHistory().DuplicateObjects(selection_.GetIds());

		if (createdIds.empty()) {
			ShowError("Could not duplicate the selected objects.");
			return;
		}

		if (!selection_.Set(std::move(createdIds))) {
			ShowError("Could not select the duplicated objects.");
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "EditorScene: 物体の一括複製に失敗しました: {}", error.what()));

		ShowError("Could not duplicate the selected objects. "
		          "See the log for details.");
	}
}

void EditorScene::DeleteSelectedObjects() {
	if (!HasSelection()) {
		return;
	}

	try {
		if (!document_.GetHistory().DeleteObjects(selection_.GetIds())) {
			ShowError("Could not delete the selected objects.");
			return;
		}

		selection_.Clear();
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "EditorScene: 物体の一括削除に失敗しました: {}", error.what()));

		ShowError("Could not delete the selected objects. "
		          "See the log for details.");
	}
}

void EditorScene::CopySelectedObjects(bool cut) {
	if (!HasSelection()) {
		return;
	}
	try {
		if (!clipboard_.Copy(document_.GetScene(), selection_.GetIds())) {
			ShowError("Could not copy the selected objects.");
			return;
		}
		if (cut) {
			DeleteSelectedObjects();
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene: 物体のコピーに失敗しました: {}",
		                          error.what()));
		ShowError("Could not copy objects. See the log for details.");
	}
}

void EditorScene::PasteObjects() {
	if (!clipboard_.HasObjects()) {
		return;
	}
	try {
		const auto copied = clipboard_.GetObjects();
		std::vector<SceneObject> objects(copied.begin(), copied.end());
		for (auto &object : objects) {
			object.name += " Copy";
		}
		auto ids = document_.GetHistory().CreateObjects(std::move(objects));
		if (ids.empty()) {
			ShowError("Could not paste the copied objects.");
			return;
		}
		selection_.Set(std::move(ids));
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene: 物体の貼り付けに失敗しました: {}",
		                          error.what()));
		ShowError("Could not paste objects. See the log for details.");
	}
}

void EditorScene::ResetSelectedTransforms() {
	if (!HasSelection()) {
		return;
	}
	try {
		if (!document_.GetHistory().ResetTransforms(selection_.GetIds())) {
			ShowError("Could not reset the selected transforms.");
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene: Transformの初期化に失敗しました: {}",
		                          error.what()));
		ShowError("Could not reset transforms. See the log for details.");
	}
}

bool EditorScene::HasSelection() const {
	if (selection_.GetCount() == 0) {
		return false;
	}

	const Scene &scene = document_.GetScene();

	for (const SceneObjectId id : selection_.GetIds()) {
		if (!scene.Contains(id)) {
			return false;
		}
	}

	return true;
}

bool EditorScene::HasSingleSelection() const {
	return selection_.GetCount() == 1 &&
	       document_.GetScene().Contains(selection_.GetActiveId());
}
