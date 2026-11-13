#include "dialogs/SaveChangesDialog.h"

#include "graphics/ui/ImGuiSystem.h"

#include <utility>

void SaveChangesDialog::Show(std::string name, bool beforeExport,
                             bool returnToOpenScene) {
	name_ = std::move(name);
	beforeExport_ = beforeExport;
	returnToOpenScene_ = returnToOpenScene;
	openRequested_ = true;
}

SaveChangesResult SaveChangesDialog::Draw(const ImGuiSystem &ui) {
	constexpr const char *kTitle = "Save changes?###SaveChangesDialog";

	if (openRequested_) {
		ui.OpenPopup(kTitle);
		openRequested_ = false;
	}

	if (!ui.BeginPopupModal(kTitle)) {
		return SaveChangesResult::NONE;
	}

	ui.Text(beforeExport_ ? "Unsaved scene changes:" : "Save changes to:");
	ui.Text(name_);
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
