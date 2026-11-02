#include "gameplay/EditorPlayMode.h"
#include "gameplay/GameplayInput.h"
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
      previewRenderer_(preview_.GetScene(), models, render), render_(render) {
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
                                              bool canKeepTransforms) {
	visible_ = false;
	ready_ = false;
	viewport_ = {};

	if (!IsPlaying()) {
		gameWidth_ = config.width;
		gameHeight_ = config.height;
	}

	ui.SetNextWindowSize(800, 500);

	if (focusRequested_) {
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
		focusRequested_ = false;
	}

	if (!ui.BeginWindow("Game")) {
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
		ui.BeginDisabled(!canKeepTransforms);

		if (ui.Button("Stop and Keep Selected Transforms")) {
			action = Action::STOP_AND_KEEP_TRANSFORMS;
		}

		ui.EndDisabled();
	} else {
		ui.BeginDisabled(!canStart);

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
                           SceneObjectId *invalidObject) {
	if (invalidObject) {
		*invalidObject = 0;
	}

	if (config.width < 1 || config.width > 8192 || config.height < 1 ||
	    config.height > 8192) {
		error = "Game resolution must be between 1 and 8192 pixels.";
		return false;
	}

	if (!session_.Start(scene, error, fallbackPose, invalidObject)) {
		return false;
	}

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
	ready_ = false;
	visible_ = false;
	focusRequested_ = false;
	viewport_ = {};
}

bool EditorPlayMode::Update(float seconds, const InputSystem &input,
                            bool inputBlocked, bool sceneVisible) {
	if (!IsPlaying()) {
		return true;
	}

	const auto controls =
	    inputBlocked ? CharacterInput{}
	                 : GameplayInput::Read(input.GetKeyboard(), viewport_,
	                                       ImGui::GetIO().WantTextInput);

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

	if (!renderer_.SetCameraPose(session_.GetCameraPose()) ||
	    !renderer_.SetCameraSettings(session_.GetCameraSettings()) ||
	    !renderer_.Update(texture_->GetWidth(), texture_->GetHeight())) {
		return false;
	}

	previewFrame_ = false;
	ready_ = true;
	return true;
}

bool EditorPlayMode::UpdatePreview(const Scene &scene,
                                   const CameraPose &fallback) {
	if (IsPlaying() || !visible_ || !texture_) {
		return true;
	}

	// 無効なカメラでは過去のシーンを捨て、空のワールドを描画して古い画像を残さない。
	preview_.Prepare(scene, fallback);

	if (!previewRenderer_.SetCameraPose(preview_.GetPose()) ||
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
	previewFrame_ = false;
}
