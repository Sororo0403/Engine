#include "dialogs/ObjectArrayDialog.h"

#include "graphics/ui/ImGuiSystem.h"
#include "scene/Scene.h"
#include <format>
#include <imgui.h>
#include <iterator>
#include <set>
#include <utility>

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
	spacingFace_.reset();
	spacingError_.clear();
	fittedCopies_ = 0;
	openRequested_ = true;
	return true;
}

ObjectArrayResult ObjectArrayDialog::Draw(const ImGuiSystem &ui,
                                          const Scene &scene) {
	constexpr const char *kTitle = "Duplicate with Offset###ObjectArrayDialog";

	if (openRequested_) {
		ui.OpenPopup(kTitle);
		openRequested_ = false;
	}

	ImGui::SetNextWindowSize({460.0f, 0}, ImGuiCond_Appearing);

	if (!ui.BeginPopupModal(kTitle)) {
		return ObjectArrayResult::NONE;
	}

	ImGui::InputInt("Copies", &settings_.copies, 1, 10);
	if (ui.DragFloat3("World Offset", settings_.offset, 0.1f, 0, 0)) {
		spacingFace_.reset();
		spacingError_.clear();
	}
	ImGui::SeparatorText("Spacing from Colliders");
	const std::pair<const char *, ColliderPlacement::Face> directions[] = {
	    {"Spacing +X", ColliderPlacement::Face::RIGHT},
	    {"Spacing -X", ColliderPlacement::Face::LEFT},
	    {"Spacing +Y", ColliderPlacement::Face::ABOVE},
	    {"Spacing -Y", ColliderPlacement::Face::BELOW},
	    {"Spacing +Z", ColliderPlacement::Face::FRONT},
	    {"Spacing -Z", ColliderPlacement::Face::BACK}};
	bool fitRequested = false;
	ui.BeginDisabled(!settings_.IsValid(sourceIds_.size()) ||
	                 !ColliderPlacement::CanDrop(scene, sourceIds_));
	for (std::size_t i = 0; i < std::size(directions); ++i) {
		if (i % 2 != 0) {
			ui.SameLine();
		}
		if (ui.Button(directions[i].first)) {
			spacingFace_ = directions[i].second;
			fitRequested = true;
		}
	}
	ui.EndDisabled();
	const auto fit = [&] {
		fittedCopies_ = settings_.copies;
		return ColliderPlacement::GetRepeatTranslation(
		    scene, sourceIds_, *spacingFace_, settings_.copies,
		    settings_.offset, spacingError_);
	};
	if (spacingFace_ && (fitRequested || fittedCopies_ != settings_.copies)) {
		fit();
	}
	const bool validSettings = settings_.IsValid(sourceIds_.size());
	const bool valid = validSettings && spacingError_.empty();
	if (!spacingError_.empty()) {
		ImGui::TextWrapped("%s", spacingError_.c_str());
	}

	if (validSettings) {
		ui.Text(std::format("New objects: {}",
		                    sourceIds_.size() *
		                        static_cast<std::size_t>(settings_.copies)));
	} else {
		ui.Text(
		    "Use 1-100 copies, at most 10000 new objects, and finite offsets.");
	}

	ObjectArrayResult result = ObjectArrayResult::NONE;
	ui.BeginDisabled(!valid);

	if (ui.Button("Create Copies") && (!spacingFace_ || fit())) {
		result = ObjectArrayResult::ACCEPTED;
	}

	ui.EndDisabled();
	ui.SameLine();

	if (result == ObjectArrayResult::NONE &&
	    (ui.Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
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
