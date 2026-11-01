#include "gameplay/EditorPlayMode.h"
#include "gameplay/GameplayInput.h"
#include "graphics/render/RenderSystem.h"
#include "graphics/render/RenderTexture.h"
#include "graphics/ui/ImGuiSystem.h"
#include "input/InputSystem.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>

EditorPlayMode::EditorPlayMode(ModelManager &models, RenderSystem &render)
    : renderer_(simulation_.GetScene(), models, render), render_(render) {}
EditorPlayMode::~EditorPlayMode() = default;
bool EditorPlayMode::Initialize() { return renderer_.Initialize(); }
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
		ImGui::TextUnformatted(
		    "WASD: move | Space: jump | Esc: stop. Click Game to control.");
	} else {
		ui.BeginDisabled(!canStart);
		if (ui.Button("Play")) {
			action = Action::PLAY;
		}
		ImGui::SameLine();
		if (ui.Button("New Sample Stage...")) {
			action = Action::SAMPLE;
		}
		ui.EndDisabled();
		ImGui::TextWrapped(
		    "Add Box Collider, Character Controller, Follow Camera and Respawn "
		    "in Inspector. Play uses a separate copy; Stop keeps your edits "
		    "and Undo history.");
	}
	if (IsPlaying()) {
		const auto size = ui.GetContentSize();
		if (std::isfinite(size.x) && std::isfinite(size.y) && size.x >= 1 &&
		    size.y >= 1) {
			const int width = int(std::clamp(size.x, 1.0f, 8192.0f)),
			          height = int(std::clamp(size.y, 1.0f, 8192.0f));
			if (!texture_) {
				texture_ = render_.CreateRenderTexture(width, height);
			}
			if (texture_ && texture_->Resize(width, height)) {
				ui.Image(*texture_, float(width), float(height));
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
bool EditorPlayMode::Start(const Scene &scene, std::string &error) {
	if (!simulation_.Start(scene, error)) {
		return false;
	}
	focusRequested_ = true;
	return true;
}
void EditorPlayMode::Stop() {
	simulation_.Stop();
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
	if (!simulation_.Update(seconds, controls)) {
		return false;
	}
	if (!renderer_.SetCameraPose(simulation_.GetCameraPose()) ||
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
