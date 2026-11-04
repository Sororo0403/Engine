#include "gameplay/EditorPlayMode.h"
#include "gameplay/GameplayInput.h"
#include "graphics/model/ModelManager.h"
#include "graphics/render/RenderSystem.h"
#include "graphics/render/RenderTexture.h"
#include "graphics/ui/ImGuiSystem.h"
#include "input/InputSystem.h"
#include "layout/GameViewLayout.h"
#include <cmath>
#include <format>
#include <imgui.h>
#include <initializer_list>

EditorPlayMode::EditorPlayMode(ModelManager &models, RenderSystem &render)
    : renderer_(session_.GetScene(), models, render),
      previewRenderer_(preview_.GetScene(), models, render,
                       SceneRenderer::ModelLoadFailure::SKIP),
      render_(render), models_(models) {
}

EditorPlayMode::~EditorPlayMode() = default;

bool EditorPlayMode::Initialize() {
	if (!renderer_.Initialize()) {
		return false;
	}

	if (!previewRenderer_.Initialize()) {
		renderer_.Finalize();
		return false;
	}

	return true;
}

EditorPlayMode::Action EditorPlayMode::DrawUI(const ImGuiSystem &ui,
                                              bool canStart,
                                              const GameConfig &config,
                                              bool canUseSelection,
                                              const Scene *editingScene) {
	visible_ = false;
	ready_ = false;
	viewport_ = {};

	// ウィンドウの切り替えは、ボタンを描いた次のフレームから反映する。
	if (maximizeRequested_) {
		maximized_ = !maximized_;
		maximizeRequested_ = false;
		focusRequested_ = true;
	}

	if (!IsPlaying()) {
		lighting_ = config.lighting;
		gameWidth_ = config.width;
		gameHeight_ = config.height;
	}

	ui.SetNextWindowSize(800, 500);

	const bool focusWindow = focusRequested_;

	if (focusRequested_) {
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
		focusRequested_ = false;
	}

	bool open = false;

	if (maximized_) {
		const auto *viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);
		open = ImGui::Begin(
		    "Game###MaximizedGame", nullptr,
		    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
		        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	} else {
		open = ui.BeginWindow("Game");
	}

	if (focusWindow && !maximized_) {
		ui.FocusWindow("Game");
	}

	if (!open) {
		ui.EndWindow();
		return Action::NONE;
	}

	Action action = Action::NONE;

	if (IsPlaying()) {
		if (ui.Button("Stop")) {
			action = Action::STOP;
		}

		ImGui::SameLine();

		if (ui.Button("Restart")) {
			action = Action::RESTART;
		}

		ImGui::SameLine();

		if (ui.Button(session_.IsPaused() ? "Resume" : "Pause")) {
			action = Action::TOGGLE_PAUSE;
		}

		ImGui::SameLine();
		ui.BeginDisabled(!session_.IsPaused());

		if (ui.Button("Step")) {
			action = Action::STEP;
		}

		ui.EndDisabled();
		ui.BeginDisabled(!canStart);
		ImGui::SetNextItemWidth(110.0f);
		const auto speedLabel = std::format("{:g}x", session_.GetTimeScale());

		if (ImGui::BeginCombo("Playback Speed", speedLabel.c_str())) {
			for (const float speed : {0.1f, 0.25f, 0.5f, 1.0f}) {
				const auto label = std::format("{:g}x", speed);

				if (ui.Selectable(label.c_str(),
				                  session_.GetTimeScale() == speed)) {
					session_.SetTimeScale(speed);
				}
			}

			ImGui::EndCombo();
		}

		ui.EndDisabled();
		ui.BeginDisabled(!canUseSelection);

		if (ui.Button("Stop and Select in Editing Scene")) {
			action = Action::STOP_AND_SELECT;
		}

		if (ui.Button("Stop and Keep Selected Transforms")) {
			action = Action::STOP_AND_KEEP_TRANSFORMS;
		}

		ui.EndDisabled();
		ui.BeginDisabled(!canStart);

		if (ui.Button(maximized_ ? "Restore Layout" : "Maximize Game")) {
			maximizeRequested_ = true;
		}

		ui.EndDisabled();
	} else {
		if (previewCamera_ != 0 && editingScene) {
			const auto *object = editingScene->FindObject(previewCamera_);

			if (!object || !object->camera) {
				SetPreviewCamera(0);
			}
		}

		ui.BeginDisabled(!canStart);

		if (previewCamera_ != 0) {
			const auto *object = editingScene
			                         ? editingScene->FindObject(previewCamera_)
			                         : nullptr;
			ui.Text(std::format("Preview Camera: {} (ID {})",
			                    object && !object->name.empty() ? object->name
			                                                    : "Camera",
			                    previewCamera_));

			if (ui.Button("Preview Primary Camera")) {
				SetPreviewCamera(0);
			}
		}

		if (ui.Button("Play")) {
			action = Action::PLAY;
		}

		ui.EndDisabled();
	}

	{
		const auto layout =
		    GameViewLayout::Fit(ui.GetContentSize(), gameWidth_, gameHeight_);

		if (layout) {
			if (!texture_) {
				texture_ = render_.CreateRenderTexture(gameWidth_, gameHeight_);
			}

			if (texture_ && texture_->Resize(gameWidth_, gameHeight_)) {
				const auto cursor = ImGui::GetCursorPos();
				ImGui::SetCursorPos(
				    {cursor.x + layout->offset.x, cursor.y + layout->offset.y});
				ui.Image(*texture_, layout->size.x, layout->size.y);
				viewport_ = ui.GetViewportInput();
				visible_ = true;
			} else {
				action = Action::RENDER_ERROR;
			}
		}
	}

	ui.EndWindow();
	return action;
}

bool EditorPlayMode::Start(const Scene &scene, std::string &error,
                           const CameraPose &fallbackPose,
                           const GameConfig &config,
                           SceneObjectId *invalidObject,
                           SceneObjectId cameraOverride) {
	if (invalidObject) {
		*invalidObject = 0;
	}

	if (config.width < 1 || config.width > 8192 || config.height < 1 ||
	    config.height > 8192) {
		error = "Game resolution must be between 1 and 8192 pixels.";
		return false;
	}

	if (!config.lighting.IsValid()) {
		error = "Choose valid lighting before Play.";
		return false;
	}

	if (!config.input.IsValid()) {
		error = "Choose different supported gameplay keys before Play.";
		return false;
	}

	for (const auto &[id, object] : scene.GetObjects()) {
		if (object.IsRenderable() && !models_.Load(object.mesh->modelPath)) {
			error = std::format("Could not load the model for object \"{}\" "
			                    "(ID {}). Repair its Mesh before Play.",
			                    object.name, id);
			if (invalidObject) {
				*invalidObject = id;
			}
			return false;
		}
	}

	if (!session_.Start(scene, error, fallbackPose, invalidObject,
	                    cameraOverride)) {
		return false;
	}

	lighting_ = config.lighting;
	inputBindings_ = config.input;
	cameraRelativeMovement_ = config.cameraRelativeMovement;
	gameWidth_ = config.width;
	gameHeight_ = config.height;
	ready_ = false;
	visible_ = false;
	viewport_ = {};
	focusRequested_ = true;
	return true;
}

bool EditorPlayMode::Restart(std::string &error) {
	if (!session_.Restart(error)) {
		return false;
	}

	ready_ = false;
	visible_ = false;
	viewport_ = {};
	focusRequested_ = true;
	return true;
}

void EditorPlayMode::TogglePause() {
	session_.TogglePause();
}

void EditorPlayMode::RequestStep() {
	session_.RequestStep();
}

bool EditorPlayMode::IsPaused() const {
	return session_.IsPaused();
}

void EditorPlayMode::Stop() {
	session_.Stop();
	maximized_ = false;
	maximizeRequested_ = false;
	ready_ = false;
	visible_ = false;
	focusRequested_ = false;
	viewport_ = {};
}

void EditorPlayMode::RestoreLayout() {
	if (!maximized_ && !maximizeRequested_) {
		return;
	}

	maximized_ = false;
	maximizeRequested_ = false;
	focusRequested_ = false;
}

bool EditorPlayMode::Update(float seconds, const InputSystem &input,
                            bool inputBlocked, bool sceneVisible) {
	if (!IsPlaying()) {
		return true;
	}

	const auto *movementCamera =
	    cameraRelativeMovement_ ? &session_.GetCameraPose() : nullptr;
	const auto controls =
	    inputBlocked ? CharacterInput{}
	                 : GameplayInput::Read(input.GetKeyboard(), viewport_,
	                                       ImGui::GetIO().WantTextInput,
	                                       inputBindings_, movementCamera);

	if (!inputBlocked && viewport_.windowFocused &&
	    viewport_.applicationFocused && !viewport_.widgetActive &&
	    !ImGui::GetIO().WantTextInput) {
		const auto &keys = input.GetKeyboard();

		if (keys.IsPressed(Key::ESCAPE)) {
			Stop();
			return true;
		}
	}

	// Sceneだけが表示されている間も再生し、アプリ全体のフォーカス喪失時は止める。
	if ((!visible_ && !sceneVisible) || ImGui::GetIO().AppFocusLost ||
	    (!sceneVisible && !viewport_.applicationFocused)) {
		return true;
	}

	if (!std::isfinite(seconds) || seconds < 0) {
		return false;
	}

	if (!session_.Update(seconds, controls)) {
		return false;
	}

	if (!visible_) {
		return true;
	}

	if (!renderer_.SetLighting(lighting_) ||
	    !renderer_.SetCameraPose(session_.GetCameraPose()) ||
	    !renderer_.SetCameraSettings(session_.GetCameraSettings()) ||
	    !renderer_.Update(texture_->GetWidth(), texture_->GetHeight())) {
		return false;
	}

	previewFrame_ = false;
	ready_ = true;
	return true;
}

bool EditorPlayMode::SetPreviewCamera(SceneObjectId objectId) {
	if (IsPlaying()) {
		return false;
	}

	previewCamera_ = objectId;
	focusRequested_ = objectId != 0;
	return true;
}

bool EditorPlayMode::UpdatePreview(const Scene &scene,
                                   const CameraPose &fallback) {
	if (!IsPlaying() && previewCamera_ != 0) {
		const auto *object = scene.FindObject(previewCamera_);

		if (!object || !object->camera) {
			SetPreviewCamera(0);
		}
	}

	if (IsPlaying() || !visible_ || !texture_) {
		return true;
	}

	// 無効なカメラでは過去のシーンを捨て、空のワールドを描画して古い画像を残さない。
	preview_.Prepare(scene, fallback, previewCamera_);

	if (!previewRenderer_.SetLighting(lighting_) ||
	    !previewRenderer_.SetCameraPose(preview_.GetPose()) ||
	    !previewRenderer_.SetCameraSettings(preview_.GetSettings()) ||
	    !previewRenderer_.Update(texture_->GetWidth(), texture_->GetHeight())) {
		return false;
	}

	previewFrame_ = true;
	ready_ = true;
	return true;
}

void EditorPlayMode::Draw() const {
	if (ready_ && texture_) {
		if (previewFrame_) {
			previewRenderer_.Draw(texture_.get());
		} else if (IsPlaying()) {
			renderer_.Draw(texture_.get());
		}
	}
}

const EditorPlaySession &EditorPlayMode::GetSession() const {
	return session_;
}

void EditorPlayMode::Finalize() {
	Stop();
	texture_.reset();
	renderer_.Finalize();
	previewRenderer_.Finalize();
	preview_.Reset();
	previewCamera_ = 0;
	previewFrame_ = false;
}
