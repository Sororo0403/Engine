#include "gameplay/MotionPathInspector.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "scene/SceneObject.h"
#include <algorithm>
#include <format>
#include <imgui.h>
#include <string>

bool MotionPathInspector::Draw(const ImGuiSystem &ui, SceneObjectId id,
                               SceneObject &object,
                               EditorHistory &history) const {
	if (!object.motionPath || !ui.CollapsingHeader("Motion Path"))
		return false;
	const auto scope = std::to_string(id);
	ImGui::PushID(scope.c_str());
	ImGui::PushID("MotionPathInspector");
	bool changed = false;
	auto edited = *object.motionPath;
	const auto apply = [&](bool valueChanged) {
		const bool begin =
		    !ImGui::IsItemActivated() || history.BeginMotionPath(id);
		if (begin && valueChanged && edited.IsValid() &&
		    edited != *object.motionPath &&
		    (history.IsEditing() || history.BeginMotionPath(id))) {
			object.motionPath = edited;
			changed = true;
		}
		if (ImGui::IsItemDeactivated() && !ImGui::IsAnyItemActive())
			history.CommitEdit();
	};
	const char *modes[]{"Once", "Loop", "Ping Pong"};
	const int current = static_cast<int>(edited.mode);
	if (ImGui::BeginCombo("Playback", current >= 0 && current < 3
	                                      ? modes[current]
	                                      : "Invalid")) {
		for (int i = 0; i < 3; ++i)
			if (ImGui::Selectable(modes[i], i == current)) {
				edited.mode = static_cast<MotionPathComponent::Mode>(i);
				changed |= history.SetMotionPath(id, edited);
			}
		ImGui::EndCombo();
	}
	apply(ImGui::DragFloat("Segment Seconds", &edited.segmentSeconds, .01f,
	                       MotionPathComponent::kMinimumSegmentSeconds,
	                       MotionPathComponent::kMaximumSegmentSeconds, "%.3f",
	                       ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::DragFloat("Waypoint Wait (s)", &edited.waypointWaitSeconds,
	                       .01f, 0,
	                       MotionPathComponent::kMaximumWaypointWaitSeconds,
	                       "%.3f", ImGuiSliderFlags_AlwaysClamp));
	if (ImGui::Checkbox("Play on Start", &edited.playOnStart))
		changed |= history.SetMotionPath(id, edited);
	if (ImGui::Checkbox("Smooth", &edited.smooth))
		changed |= history.SetMotionPath(id, edited);
	ui.Text("Point 0: 0, 0, 0");
	std::size_t remove = edited.points.size();

	for (std::size_t i = 1; i < edited.points.size(); ++i) {
		const auto label = std::format("Point {}", i);
		apply(ImGui::DragFloat3(label.c_str(), edited.points[i].data(), .05f,
		                        -MotionPathComponent::kMaximumOffset,
		                        MotionPathComponent::kMaximumOffset, "%.3f",
		                        ImGuiSliderFlags_AlwaysClamp));
		ImGui::PushID(static_cast<int>(i));
		ImGui::BeginDisabled(edited.points.size() <= 2);
		if (ImGui::SmallButton("Remove Point"))
			remove = i;
		ImGui::EndDisabled();
		ImGui::PopID();
	}

	if (remove < edited.points.size()) {
		edited.points.erase(edited.points.begin() +
		                    static_cast<std::ptrdiff_t>(remove));
		changed |= history.SetMotionPath(id, edited);
	}

	ImGui::BeginDisabled(edited.points.size() >=
	                     MotionPathComponent::kMaximumPoints);
	if (ImGui::Button("Add Path Point")) {
		auto point = edited.points.back();
		point[1] =
		    (std::min)(MotionPathComponent::kMaximumOffset, point[1] + 3);
		edited.points.push_back(point);
		changed |= history.SetMotionPath(id, edited);
	}

	ImGui::EndDisabled();
	if (ImGui::Button("Reset Motion Path"))
		changed |= history.SetMotionPath(id, MotionPathComponent{});
	if (ImGui::Button("Remove Motion Path"))
		changed |= history.SetMotionPath(id, std::nullopt);
	ImGui::PopID();
	ImGui::PopID();
	return changed;
}

void MotionPathInspector::DrawReadOnly(const ImGuiSystem &ui,
                                       const SceneObject &object) const {
	if (!object.motionPath || !ui.CollapsingHeader("Motion Path"))
		return;
	const auto &path = *object.motionPath;
	ui.Text(std::format("Points: {}", path.points.size()));
	ui.Text(path.playOnStart ? "Play on Start: Yes" : "Play on Start: No");
	ui.Text(std::format("Segment Seconds: {:.3f}", path.segmentSeconds));
	ui.Text(std::format("Waypoint Wait: {:.3f} s", path.waypointWaitSeconds));
	ui.Text(path.mode == MotionPathComponent::Mode::ONCE ? "Playback: Once"
	        : path.mode == MotionPathComponent::Mode::LOOP
	            ? "Playback: Loop"
	            : "Playback: Ping Pong");
}
