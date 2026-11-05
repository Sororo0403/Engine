#include "lighting/PointLightInspector.h"

#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "scene/SceneObject.h"
#include <format>
#include <imgui.h>
#include <string>

bool PointLightInspector::Draw(const ImGuiSystem &ui, SceneObjectId id,
                               SceneObject &object,
                               EditorHistory &history) const {
	if (!object.pointLight || !ui.CollapsingHeader("Point Light")) {
		return false;
	}
	const auto scope = std::to_string(id);
	ImGui::PushID(scope.c_str());
	ImGui::PushID("PointLightInspector");
	auto edited = *object.pointLight;
	bool changed = false;
	const auto apply = [&](bool valueChanged) {
		const bool begin =
		    !ImGui::IsItemActivated() || history.BeginPointLight(id);
		if (begin && valueChanged && edited.IsValid() &&
		    edited != *object.pointLight &&
		    (history.IsEditing() || history.BeginPointLight(id))) {
			object.pointLight = edited;
			changed = true;
		}
		if (ImGui::IsItemDeactivated() && !ImGui::IsAnyItemActive()) {
			history.CommitEdit();
		}
	};
	apply(ImGui::ColorEdit3("Color", edited.color.data()));
	apply(ImGui::DragFloat("Intensity", &edited.intensity, 0.05f, 0.0f,
	                       PointLightComponent::kMaxIntensity, "%.2f",
	                       ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::DragFloat("Range", &edited.range, 0.1f, 0.001f,
	                       PointLightComponent::kMaxRange, "%.3f",
	                       ImGuiSliderFlags_AlwaysClamp));
	if (ImGui::Button("Reset Point Light")) {
		const auto before = object.pointLight;
		changed |= history.SetPointLight(id, PointLightComponent{}) &&
		           object.pointLight != before;
	}
	if (ImGui::Button("Remove Point Light")) {
		changed |= history.SetPointLight(id, std::nullopt);
	}
	ImGui::PopID();
	ImGui::PopID();
	return changed;
}

void PointLightInspector::DrawReadOnly(const ImGuiSystem &ui,
                                       const SceneObject &object) const {
	if (!object.pointLight || !ui.CollapsingHeader("Point Light")) {
		return;
	}
	const auto &light = *object.pointLight;
	ui.Text(std::format("Color: {:.3f}, {:.3f}, {:.3f}", light.color[0],
	                    light.color[1], light.color[2]));
	ui.Text(std::format("Intensity: {:.3f}", light.intensity));
	ui.Text(std::format("Range: {:.3f}", light.range));
}
