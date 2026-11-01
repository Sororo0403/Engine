#include "gameplay/EditorPlayMode.h"
#include "gameplay/GameplayInput.h"
#include "graphics/render/RenderSystem.h"
#include "graphics/render/RenderTexture.h"
#include "graphics/ui/ImGuiSystem.h"
#include "input/InputSystem.h"
#include "layout/GameViewLayout.h"
#include <cmath>
#include <imgui.h>

EditorPlayMode::EditorPlayMode(ModelManager &models, RenderSystem &render)
    : renderer_(simulation_.GetScene(), models, render), render_(render) {
}

EditorPlayMode::~EditorPlayMode() = default;

bool EditorPlayMode::Initialize() {
	return renderer_.Initialize();
}

EditorPlayMode::Action EditorPlayMode::DrawUI(const ImGuiSystem &ui,
                                              bool canStart) {
	visible_ = false;
	ready_ = false;
	viewport_ = {};
	ui.SetNextWindowSize(800, 500);

	if (focusRequested_) {
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

		if (ui.Button(clock_.IsPaused() ? "Resume" : "Pause")) {
			clock_.TogglePause();
		}

		ImGui::SameLine();
		ui.BeginDisabled(!clock_.IsPaused());

		if (ui.Button("Step")) {
			clock_.RequestStep();
		}

		ui.EndDisabled();
	} else {
		ui.BeginDisabled(!canStart);

		if (ui.Button("Play")) {
			action = Action::PLAY;
		}

		ui.EndDisabled();
	}

	if (IsPlaying()) {
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
                           const GameConfig &config) {
	if (config.width < 1 || config.width > 8192 || config.height < 1 ||
	    config.height > 8192) {
		error = "Game resolution must be between 1 and 8192 pixels.";
		return false;
	}

	if (!simulation_.Start(scene, error, fallbackPose)) {
		return false;
	}

	gameWidth_ = config.width;
	gameHeight_ = config.height;
	clock_.Reset();
	focusRequested_ = true;
	return true;
}

void EditorPlayMode::Stop() {
	simulation_.Stop();
	clock_.Reset();
	ready_ = false;
	viewport_ = {};
}

bool EditorPlayMode::Update(float seconds, const InputSystem &input) {
	if (!IsPlaying()) {
		return true;
	}

	const auto controls = GameplayInput::Read(input.GetKeyboard(), viewport_,
	                                          ImGui::GetIO().WantTextInput);

	if (viewport_.windowFocused && viewport_.applicationFocused &&
	    !viewport_.widgetActive && !ImGui::GetIO().WantTextInput) {
		const auto &keys = input.GetKeyboard();

		if (keys.IsPressed(Key::ESCAPE)) {
			Stop();
			return true;
		}
	}

	// 非表示やフォーカス喪失時はゲーム時間も止め、復帰時の大きな移動を防ぐ。
	if (!visible_ || !viewport_.applicationFocused) {
		return true;
	}

	if (!std::isfinite(seconds) || seconds < 0) {
		return false;
	}

	const auto gameTime = clock_.Advance(seconds);

	if (gameTime && !simulation_.Update(*gameTime, controls)) {
		return false;
	}

	if (!renderer_.SetCameraPose(simulation_.GetCameraPose()) ||
	    !renderer_.SetCameraSettings(simulation_.GetCameraSettings()) ||
	    !renderer_.Update(texture_->GetWidth(), texture_->GetHeight())) {
		return false;
	}

	ready_ = true;
	return true;
}

void EditorPlayMode::Draw() const {
	if (ready_ && IsPlaying() && texture_) {
		renderer_.Draw(texture_.get());
	}
}

void EditorPlayMode::Finalize() {
	Stop();
	texture_.reset();
	renderer_.Finalize();
}
