#include "dialogs/SaveChangesDialog.h"

#include "graphics/ui/ImGuiSystem.h"

#include <imgui.h>
#include <utility>

void SaveChangesDialog::Show(std::string name, bool beforeExport,
                             bool returnToOpenScene, std::string destination,
                             bool recovered) {
	name_ = std::move(name);
	destination_ = std::move(destination);
	beforeExport_ = beforeExport;
	returnToOpenScene_ = returnToOpenScene;
	recovered_ = recovered;
	openRequested_ = true;
}

SaveChangesResult SaveChangesDialog::Draw(const ImGuiSystem &ui) {
	constexpr const char *kTitle = "Save changes?###SaveChangesDialog";

	if (openRequested_) {
		ui.OpenPopup(kTitle);
		openRequested_ = false;
	}

	ImGui::SetNextWindowSize({540.0f, 0.0f}, ImGuiCond_Appearing);
	if (!ui.BeginPopupModal(kTitle)) {
		return SaveChangesResult::NONE;
	}

	if (recovered_) {
		ui.Text("Save recovered scene as a new file?");
		if (!name_.empty()) {
			ui.Text("Original scene:");
			ImGui::TextWrapped("%s", name_.c_str());
		}
	} else {
		ui.Text(beforeExport_ ? "Unsaved scene changes:" : "Save changes to:");
		ImGui::TextWrapped("%s", name_.c_str());
	}
	if (!destination_.empty()) {
		ui.Text("Next location:");
		ImGui::TextWrapped("%s", destination_.c_str());
	}
	if (beforeExport_) {
		ui.Text("Export reads linked scenes from disk. Save changes to include them.");
	}
	ui.Separator();

	SaveChangesResult result = SaveChangesResult::NONE;

	if (ui.Button(beforeExport_ ? "Save and Continue" : "Save")) {
		result = SaveChangesResult::SAVE;
	}

	ui.SameLine();

	if (ui.Button(beforeExport_ ? "Continue Without Saving" : "Don't Save")) {
		result = SaveChangesResult::DISCARD;
	}

	ui.SameLine();

	if (ui.Button(returnToOpenScene_ ? "Back to Open Scene" : "Cancel")) {
		result = SaveChangesResult::CANCEL;
	}

	if (result != SaveChangesResult::NONE) {
		ui.CloseCurrentPopup();
	}

	ui.EndPopup();
	return result;
}
