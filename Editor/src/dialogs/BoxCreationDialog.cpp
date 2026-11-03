#include "dialogs/BoxCreationDialog.h"

#include "graphics/ui/ImGuiSystem.h"
#include "scene/PrimitiveObjects.h"
#include <imgui.h>

void BoxCreationDialog::Show(const BoxCollider &bounds) {
	settings_ = {};
	bounds_ = bounds;
	opening_ = true;
}

BoxCreationResult BoxCreationDialog::Draw(const ImGuiSystem &ui) {
	constexpr const char *title = "Box with Size###BoxCreationDialog";
	if (opening_) {
		ui.OpenPopup(title);
		opening_ = false;
	}
	ImGui::SetNextWindowSize({460, 0}, ImGuiCond_Appearing);
	if (!ui.BeginPopupModal(title)) {
		return BoxCreationResult::NONE;
	}

	ui.InputText("Name", settings_.name);
	ImGui::InputFloat3("Center", &settings_.center.x);
	ImGui::InputFloat3("Size", &settings_.size.x);
	SceneObject candidate;
	std::string error;
	const bool valid =
	    PrimitiveObjects::TryCreateBox(settings_, bounds_, candidate, error);
	if (!valid) {
		ImGui::TextWrapped("%s", error.c_str());
	}

	BoxCreationResult result = BoxCreationResult::NONE;
	ui.BeginDisabled(!valid);
	if (ui.Button("Create Box")) {
		result = BoxCreationResult::ACCEPTED;
	}
	ui.EndDisabled();
	ui.SameLine();
	if (result == BoxCreationResult::NONE &&
	    (ui.Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
		result = BoxCreationResult::CANCELLED;
	}
	if (result != BoxCreationResult::NONE) {
		ui.CloseCurrentPopup();
	}
	ui.EndPopup();
	return result;
}
