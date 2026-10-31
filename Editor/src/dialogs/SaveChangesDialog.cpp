#include "dialogs/SaveChangesDialog.h"

#include "graphics/ui/ImGuiSystem.h"

#include <utility>

void SaveChangesDialog::Show(std::string name) {
	name_ = std::move(name);
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

	ui.Text("Save changes to:");
	ui.Text(name_);
	ui.Separator();

	SaveChangesResult result = SaveChangesResult::NONE;

	if (ui.Button("Save")) {
		result = SaveChangesResult::SAVE;
	}

	ui.SameLine();

	if (ui.Button("Don't Save")) {
		result = SaveChangesResult::DISCARD;
	}

	ui.SameLine();

	if (ui.Button("Cancel")) {
		result = SaveChangesResult::CANCEL;
	}

	if (result != SaveChangesResult::NONE) {
		ui.CloseCurrentPopup();
	}

	ui.EndPopup();
	return result;
}
