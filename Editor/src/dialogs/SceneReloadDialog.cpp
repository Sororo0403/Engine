#include "dialogs/SceneReloadDialog.h"

#include "graphics/ui/ImGuiSystem.h"

#include <imgui.h>
#include <utility>

void SceneReloadDialog::Show(std::string name, bool modified) {
	name_ = std::move(name);
	modified_ = modified;
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
	ImGui::TextWrapped("Reload clears undo history.");
	SceneReloadResult result = SceneReloadResult::NONE;

	if (modified_) {
		ImGui::TextWrapped("This scene has unsaved edits.");
		if (ui.Button("Save Copy and Reload...")) {
			result = SceneReloadResult::SAVE_COPY_AND_RELOAD;
		}
		ui.SameLine();
	}

	if (ui.Button(modified_ ? "Discard Edits and Reload" : "Reload Scene")) {
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
