#include "lighting/SpotLightInspector.h"

#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "scene/SceneObject.h"
#include <algorithm>
#include <format>
#include <imgui.h>
#include <string>

bool SpotLightInspector::Draw(const ImGuiSystem &ui, SceneObjectId id,
                              SceneObject &object,
                              EditorHistory &history) const {
	if (!object.spotLight || !ui.CollapsingHeader("Spot Light")) {
		return false;
	}

	const auto scope = std::to_string(id);
	ImGui::PushID(scope.c_str());
	ImGui::PushID("SpotLightInspector");
	auto edited = *object.spotLight;
	bool changed = false;
	const auto apply = [&](bool valueChanged) {
		const bool begin =
		    !ImGui::IsItemActivated() || history.BeginSpotLight(id);
		if (begin && valueChanged && edited.IsValid() &&
		    edited != *object.spotLight &&
		    (history.IsEditing() || history.BeginSpotLight(id))) {
			object.spotLight = edited;
			changed = true;
		}
		if (ImGui::IsItemDeactivated() && !ImGui::IsAnyItemActive()) {
			history.CommitEdit();
		}
	};
	apply(ImGui::ColorEdit3("Color", edited.color.data()));
	apply(ImGui::DragFloat("Intensity", &edited.intensity, 0.05f, 0.0f,
	                       SpotLightComponent::kMaxIntensity, "%.2f",
	                       ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::DragFloat("Range", &edited.range, 0.1f, 0.001f,
	                       SpotLightComponent::kMaxRange, "%.3f",
	                       ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::DragFloat("Inner Angle", &edited.innerAngle, .5f, 0.0f,
	                       edited.outerAngle, "%.2f",
	                       ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::DragFloat(
	    "Outer Angle", &edited.outerAngle, .5f,
	    (std::max)(SpotLightComponent::kMinOuterAngle, edited.innerAngle),
	    SpotLightComponent::kMaxOuterAngle, "%.2f",
	    ImGuiSliderFlags_AlwaysClamp));
	if (ImGui::Button("Reset Spot Light")) {
		const auto before = object.spotLight;
		changed |= history.SetSpotLight(id, SpotLightComponent{}) &&
		           object.spotLight != before;
	}

	if (ImGui::Button("Remove Spot Light")) {
		changed |= history.SetSpotLight(id, std::nullopt);
	}

	ImGui::PopID();
	ImGui::PopID();
	return changed;
}

void SpotLightInspector::DrawReadOnly(const ImGuiSystem &ui,
                                      const SceneObject &object) const {
	if (!object.spotLight || !ui.CollapsingHeader("Spot Light")) {
		return;
	}

	const auto &light = *object.spotLight;
	ui.Text(std::format("Color: {:.3f}, {:.3f}, {:.3f}", light.color[0],
	                    light.color[1], light.color[2]));
	ui.Text(std::format("Intensity: {:.3f}", light.intensity));
	ui.Text(std::format("Range: {:.3f}", light.range));
	ui.Text(std::format("Angles: {:.2f} / {:.2f} degrees", light.innerAngle,
	                    light.outerAngle));
}
