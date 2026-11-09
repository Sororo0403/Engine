#include "panels/SelectionMeasurementsPanel.h"

#include "graphics/ui/ImGuiSystem.h"
#include "scene/Scene.h"
#include "selection/SelectionMeasurement.h"
#include <format>
#include <imgui.h>

void SelectionMeasurementsPanel::Draw(const ImGuiSystem &ui, const Scene &scene,
                                      std::span<const SceneObjectId> ids,
                                      bool &open) const {
	if (!open) {
		return;
	}

	ImGui::SetNextWindowSize({420, 440}, ImGuiCond_FirstUseEver);

	if (!ImGui::Begin("Selection Measurements", &open)) {
		ImGui::End();
		return;
	}

	SelectionMeasurement::Result result;
	std::string error;

	if (!SelectionMeasurement::Measure(scene, ids, result, error)) {
		ImGui::TextWrapped("%s", error.c_str());
	} else {
		ui.Text(std::format("Selected objects: {}", result.objects));
		ui.Text(std::format("Valid Colliders: {} / {}", result.colliders,
		                    result.objects));
		if (result.invalidColliders) {
			ui.Text(
			    std::format("Invalid Colliders: {}", result.invalidColliders));
		}

		if (result.colliderBounds) {
			const auto &box = *result.colliderBounds;
			ImGui::SeparatorText("Combined Collider Bounds");
			ui.Text(std::format("Size X: {:.3f}",
			                    double(box.maximum.x) - box.minimum.x));
			ui.Text(std::format("Size Y: {:.3f}",
			                    double(box.maximum.y) - box.minimum.y));
			ui.Text(std::format("Size Z: {:.3f}",
			                    double(box.maximum.z) - box.minimum.z));
		}

		if (result.pair) {
			const auto &pair = *result.pair;
			ImGui::SeparatorText("Object Origins");
			ImGui::TextWrapped("From: %s (ID %llu)",
			                   scene.FindObject(pair.from)->name.c_str(),
			                   static_cast<unsigned long long>(pair.from));
			ImGui::TextWrapped("To: %s (ID %llu)",
			                   scene.FindObject(pair.to)->name.c_str(),
			                   static_cast<unsigned long long>(pair.to));
			ui.Text(std::format("Distance: {:.3f}", pair.distance));
			ui.Text(std::format("Horizontal distance: {:.3f}",
			                    pair.horizontalDistance));
			ui.Text(std::format("Delta X: {:.3f}", pair.delta[0]));
			ui.Text(std::format("Delta Y: {:.3f}", pair.delta[1]));
			ui.Text(std::format("Delta Z: {:.3f}", pair.delta[2]));

			if (pair.colliderGap) {
				ImGui::SeparatorText("Collider Gap");
				ui.Text(
				    std::format("Gap distance: {:.3f}", pair.colliderDistance));
				ui.Text(std::format("Gap X: {:.3f}", (*pair.colliderGap)[0]));
				ui.Text(std::format("Gap Y: {:.3f}", (*pair.colliderGap)[1]));
				ui.Text(std::format("Gap Z: {:.3f}", (*pair.colliderGap)[2]));
				ui.Text(pair.collidersOverlap ? "Colliders overlap: Yes"
				                              : "Colliders overlap: No");
			}
		}
	}

	ImGui::End();
}
