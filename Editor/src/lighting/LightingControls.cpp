#include "lighting/LightingControls.h"

#include "graphics/lighting/LightingSettings.h"
#include "graphics/ui/ImGuiSystem.h"
#include <imgui.h>
#include <initializer_list>
#include <string>

void LightingControls::Draw(const ImGuiSystem &ui, LightingSettings &lighting) {
	ImGui::InputFloat3("Light Direction", &lighting.direction.x);
	ImGui::ColorEdit3("Light Color", &lighting.color.x);
	ImGui::SliderFloat("Light Intensity", &lighting.intensity, 0.0f,
	                   LightingSettings::kMaxIntensity);
	ImGui::SliderFloat("Ambient Light", &lighting.ambient, 0.0f, 1.0f);

	if (ui.Button("Reset Lighting")) {
		lighting = {};
	}

	DrawShadows(lighting.shadows);
}

void LightingControls::DrawShadows(ShadowSettings &shadows) {
	ImGui::SeparatorText("Directional Shadows");
	ImGui::Checkbox("Enable Shadows", &shadows.enabled);
	ImGui::BeginDisabled(!shadows.enabled);
	const auto resolution = std::to_string(shadows.resolution);

	if (ImGui::BeginCombo("Shadow Resolution", resolution.c_str())) {
		for (int size : {512, 1024, 2048}) {
			const auto label = std::to_string(size);
			if (ImGui::Selectable(label.c_str(),
			                      shadows.resolution == size))
				shadows.resolution = size;
		}

		ImGui::EndCombo();
	}

	ImGui::DragFloat("Shadow Extent", &shadows.extent, .1f, 1, 1000,
	                 "%.1f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Shadow Strength", &shadows.strength, 0, 1);
	ImGui::DragFloat("Shadow Depth Bias", &shadows.bias, .00001f, 0,
	                 .02f, "%.5f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::DragFloat("Shadow Normal Bias", &shadows.normalBias, .001f,
	                 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::EndDisabled();
}
