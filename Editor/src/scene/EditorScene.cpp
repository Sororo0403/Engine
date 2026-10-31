#include "scene/EditorScene.h"

#include "core/EngineContext.h"
#include "core/Logger.h"
#include "graphics/model/ModelManager.h"
#include "graphics/render/RenderTexture.h"
#include "graphics/ui/ImGuiSystem.h"
#include "input/InputSystem.h"
#include "input/Key.h"
#include "platform/windows/PathUtils.h"

#include <cmath>
#include <exception>
#include <format>
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

	cameraController_.Reset();

	stage_ = DialogStage::IDLE;
	pendingAction_ = FileAction::NONE;
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

	if (exitQueued_ && stage_ == DialogStage::IDLE) {
		exitQueued_ = false;
		StartAction(FileAction::EXIT);
	}

	FileAction action = DrawMenuBar();

	ui_.DrawDockSpace(dockLayout_, resetDockLayout_);
	resetDockLayout_ = false;

	const bool wasBusy = IsBusy();

	Scene &scene = document_.GetScene();

	ui_.BeginDisabled(wasBusy);

	const bool hierarchyChanged =
	    hierarchyPanel_.Draw(ui_, scene, selectedObjectId_);

	const bool inspectorChanged =
	    inspectorPanel_.Draw(ui_, scene, selectedObjectId_);

	const auto requestedModelPath = assetsPanel_.Draw(ui_, assets_);

	const bool sceneViewReady = sceneViewPanel_.DrawUI(ui_);

	ui_.EndDisabled();

	if (!sceneViewReady) {
		return false;
	}

	if (hierarchyChanged || inspectorChanged) {
		document_.MarkModified();
	}

	if (!wasBusy) {
		if (!requestedModelPath.empty()) {
			PlaceModel(requestedModelPath);
		}

		if (action == FileAction::NONE) {
			action = ReadShortcut();
		}

		// 配置やTransformの変更を反映してから保存する
		if (action != FileAction::NONE) {
			StartAction(action);
		}
	}

	UpdateDialogs();

	ViewportInput viewport = sceneViewPanel_.GetInput();

	if (wasBusy || IsBusy() || action != FileAction::NONE ||
	    !requestedModelPath.empty()) {
		viewport = {};
	}

	cameraController_.Update(input_, viewport, deltaTime);

	const auto *target = sceneViewPanel_.GetRenderTexture();

	if (!target) {
		return true;
	}

	if (!renderer_.SetCameraPose(cameraController_.GetPose())) {
		Logger::Error("EditorScene: カメラの姿勢が無効です。");
		return false;
	}

	return renderer_.Update(target->GetWidth(), target->GetHeight());
}

void EditorScene::Draw() const {
	auto *target = sceneViewPanel_.GetRenderTexture();

	if (target) {
		renderer_.Draw(target);
	}
}

void EditorScene::Finalize() {
	sceneViewPanel_.Finalize();
	renderer_.Finalize();

	document_.New();
	selectedObjectId_ = 0;

	stage_ = DialogStage::IDLE;
	pendingAction_ = FileAction::NONE;
	exitQueued_ = false;
	closeApproved_ = false;
	errorOpenRequested_ = false;
	errorMessage_.clear();

	assetsPanel_.Clear();
	assets_.Clear();

	cameraController_.Reset();
}

bool EditorScene::RequestClose() {
	// 終了確認中・終了許可済みの場合は重複して予約しない
	if (!closeApproved_ && pendingAction_ != FileAction::EXIT) {
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

EditorScene::FileAction EditorScene::DrawMenuBar() {
	FileAction action = FileAction::NONE;

	if (!ui_.BeginMainMenuBar()) {
		return action;
	}

	if (ui_.BeginMenu("File")) {
		const bool enabled = !IsBusy();

		if (ui_.MenuItem("New", "Ctrl+N", enabled)) {
			action = FileAction::NEW;
		}

		if (ui_.MenuItem("Open...", "Ctrl+O", enabled)) {
			action = FileAction::OPEN;
		}

		ui_.Separator();

		if (ui_.MenuItem("Save", "Ctrl+S", enabled)) {
			action = FileAction::SAVE;
		}

		if (ui_.MenuItem("Save As...", "Ctrl+Shift+S", enabled)) {
			action = FileAction::SAVE_AS;
		}

		ui_.Separator();

		if (ui_.MenuItem("Exit", nullptr, enabled)) {
			action = FileAction::EXIT;
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

EditorScene::FileAction EditorScene::ReadShortcut() const {
	if (IsBusy() || ui_.IsTextInputActive()) {
		return FileAction::NONE;
	}

	const Keyboard &keyboard = input_.GetKeyboard();

	if (!keyboard.IsDown(Key::CTRL)) {
		return FileAction::NONE;
	}

	if (keyboard.IsPressed(Key::S)) {
		return keyboard.IsDown(Key::SHIFT) ? FileAction::SAVE_AS
		                                   : FileAction::SAVE;
	}

	if (keyboard.IsPressed(Key::O)) {
		return FileAction::OPEN;
	}

	if (keyboard.IsPressed(Key::N)) {
		return FileAction::NEW;
	}

	return FileAction::NONE;
}

void EditorScene::StartAction(FileAction action) {
	if (stage_ != DialogStage::IDLE || closeApproved_) {
		return;
	}

	switch (action) {
	case FileAction::NEW:
	case FileAction::OPEN:
	case FileAction::EXIT:
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

	case FileAction::SAVE:
		pendingAction_ = FileAction::NONE;
		StartSave(false);
		return;

	case FileAction::SAVE_AS:
		pendingAction_ = FileAction::NONE;
		StartSave(true);
		return;

	case FileAction::NONE:
		return;
	}
}

void EditorScene::ExecutePendingAction() {
	const FileAction action = pendingAction_;
	pendingAction_ = FileAction::NONE;
	stage_ = DialogStage::IDLE;

	switch (action) {
	case FileAction::NEW:
		document_.New();
		selectedObjectId_ = 0;
		cameraController_.Reset();
		return;

	case FileAction::OPEN:
		fileDialog_.Show(SceneFileDialogMode::OPEN, document_.GetFilePath());

		stage_ = DialogStage::OPEN_FILE;
		return;

	case FileAction::EXIT:
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
			pendingAction_ = FileAction::NONE;
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
			pendingAction_ = FileAction::NONE;
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
	pendingAction_ = FileAction::NONE;
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

		const std::string name = PathUtils::ToUtf8(modelPath.stem());

		// ファイル読み込みに失敗したモデルはシーンへ追加しない
		const auto model = models_.Load(modelPath);

		if (!model) {
			ShowError("Could not load the model. See the log for details.");
			return;
		}

		Scene &scene = document_.GetScene();

		// Transformの初期値に従い、原点へ配置する
		selectedObjectId_ = scene.CreateObject(name, modelPath);

		document_.MarkModified();
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene::PlaceModel: {}", error.what()));

		ShowError("Could not place the model. See the log for details.");
	}
}
