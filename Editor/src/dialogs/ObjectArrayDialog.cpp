#include "dialogs/ObjectArrayDialog.h"

#include "graphics/ui/ImGuiSystem.h"
#include "scene/Scene.h"
#include <format>
#include <imgui.h>
#include <set>

bool ObjectArrayDialog::Show(const Scene &scene,
                             std::span<const SceneObjectId> ids) {
	ObjectArraySettings settings;

	if (!settings.IsValid(ids.size())) {
		return false;
	}

	std::set<SceneObjectId> seen;

	for (const auto id : ids) {
		if (!scene.Contains(id) || !seen.insert(id).second) {
			return false;
		}
	}

	std::vector<SceneObjectId> sources(ids.begin(), ids.end());
	sourceIds_.swap(sources);
	settings_ = settings;
	openRequested_ = true;
	return true;
}

ObjectArrayResult ObjectArrayDialog::Draw(const ImGuiSystem &ui) {
	constexpr const char *kTitle = "Duplicate with Offset###ObjectArrayDialog";

	if (openRequested_) {
		ui.OpenPopup(kTitle);
		openRequested_ = false;
	}

	ImGui::SetNextWindowSize({420.0f, 220.0f}, ImGuiCond_FirstUseEver);

	if (!ui.BeginPopupModal(kTitle)) {
		return ObjectArrayResult::NONE;
	}

	ImGui::InputInt("Copies", &settings_.copies, 1, 10);
	ui.DragFloat3("World Offset", settings_.offset, 0.1f, 0, 0);
	const bool valid = settings_.IsValid(sourceIds_.size());

	if (valid) {
		ui.Text(std::format("New objects: {}",
		                    sourceIds_.size() *
		                        static_cast<std::size_t>(settings_.copies)));
	} else {
		ui.Text(
		    "Use 1-100 copies, at most 10000 new objects, and finite offsets.");
	}

	ObjectArrayResult result = ObjectArrayResult::NONE;
	ui.BeginDisabled(!valid);

	if (ui.Button("Create Copies")) {
		result = ObjectArrayResult::ACCEPTED;
	}

	ui.EndDisabled();
	ui.SameLine();

	if (ui.Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		result = ObjectArrayResult::CANCELLED;
	}

	if (result != ObjectArrayResult::NONE) {
		ui.CloseCurrentPopup();
	}

	ui.EndPopup();
	return result;
}

std::span<const SceneObjectId> ObjectArrayDialog::GetSourceIds() const {
	return sourceIds_;
}

const ObjectArraySettings &ObjectArrayDialog::GetSettings() const {
	return settings_;
}
