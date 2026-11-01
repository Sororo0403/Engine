#include "dialogs/SceneReloadDialog.h"

#include "graphics/ui/ImGuiSystem.h"

#include <imgui.h>
#include <utility>

void SceneReloadDialog::Show(std::string name) {
	name_ = std::move(name);
	openRequested_ = true;
}

SceneReloadResult SceneReloadDialog::Draw(const ImGuiSystem &ui) {
	constexpr const char *kTitle = "Reload scene?###SceneReloadDialog";
	const bool opening = openRequested_;

	if (openRequested_) {
		ui.OpenPopup(kTitle);
		openRequested_ = false;
	}

	ImGui::SetNextWindowSize({520.0f, 0.0f}, ImGuiCond_Appearing);

	if (!ui.BeginPopupModal(kTitle)) {
		return SceneReloadResult::NONE;
	}

	if (opening) {
		ImGui::SetNavCursorVisible(true);
	}

	ui.Text("Reload the scene from disk:");
	ImGui::TextWrapped("%s", name_.c_str());
	ui.Separator();
	ImGui::TextWrapped("Unsaved edits will be discarded. Reload resets the "
	                   "current selection, Scene camera and undo history.");
	SceneReloadResult result = SceneReloadResult::NONE;

	if (ui.Button("Reload Scene")) {
		result = SceneReloadResult::RELOAD;
	}

	ui.SameLine();

	if (ui.Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		result = SceneReloadResult::CANCELLED;
	}

	ImGui::SetItemDefaultFocus();

	if (result != SceneReloadResult::NONE) {
		ui.CloseCurrentPopup();
	}

	ui.EndPopup();
	return result;
}
