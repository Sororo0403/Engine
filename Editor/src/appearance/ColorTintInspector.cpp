#include "appearance/ColorTintInspector.h"

#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "scene/SceneObject.h"
#include <imgui.h>
#include <string>

bool ColorTintInspector::Draw(const ImGuiSystem &ui, SceneObjectId id,
                              SceneObject &object,
                              EditorHistory &history) const {
	if (!object.colorTint) {
		return false;
	}

	const auto scope = std::to_string(id);
	ImGui::PushID(scope.c_str());
	ImGui::PushID("ColorTintInspector");
	bool changed = false;

	if (ui.CollapsingHeader("Color Tint")) {
		auto edited = *object.colorTint;
		const bool valueChanged = ui.ColorEdit3("Color", edited.color);
		const bool begin =
		    !ImGui::IsItemActivated() || history.BeginColorTint(id);

		if (begin && valueChanged && edited.IsValid() &&
		    edited != *object.colorTint &&
		    (history.IsEditing() || history.BeginColorTint(id))) {
			object.colorTint = edited;
			changed = true;
		}

		if (ImGui::IsItemDeactivated() && !ImGui::IsAnyItemActive()) {
			history.CommitEdit();
		}

		if (ui.Button("Reset Color")) {
			const auto before = object.colorTint;
			changed |= history.SetColorTint(id, ColorTint{}) &&
			           object.colorTint != before;
		}

		if (ui.Button("Remove Color Tint")) {
			changed |= history.SetColorTint(id, std::nullopt);
		}
	}

	ImGui::PopID();
	ImGui::PopID();
	return changed;
}
