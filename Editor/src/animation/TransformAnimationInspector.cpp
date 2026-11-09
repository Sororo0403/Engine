#include "animation/TransformAnimationInspector.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "math/TransformUtils.h"
#include "scene/SceneObject.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <imgui.h>
#include <limits>

bool TransformAnimationInspector::Draw(const ImGuiSystem &ui, SceneObjectId id,
                                       SceneObject &object,
                                       EditorHistory &history) const {
	if (!object.transformAnimation ||
	    !ui.CollapsingHeader("Transform Animation"))
		return false;
	const auto scope = std::to_string(id);
	ImGui::PushID(scope.c_str());
	ImGui::PushID("TransformAnimationInspector");
	bool changed = false;
	auto edited = *object.transformAnimation;
	const auto apply = [&](bool valueChanged) {
		const bool begin =
		    !ImGui::IsItemActivated() || history.BeginTransformAnimation(id);
		if (begin && valueChanged && edited.IsValid() &&
		    edited != *object.transformAnimation &&
		    (history.IsEditing() || history.BeginTransformAnimation(id))) {
			object.transformAnimation = edited;
			changed = true;
		}
		if (ImGui::IsItemDeactivated() && !ImGui::IsAnyItemActive())
			history.CommitEdit();
	};
	const char *modes[]{"Once", "Loop", "Ping Pong"};
	if (ImGui::BeginCombo("Playback", static_cast<int>(edited.mode) >= 0 &&
	                                          static_cast<int>(edited.mode) < 3
	                                      ? modes[static_cast<int>(edited.mode)]
	                                      : "Invalid")) {
		for (int i = 0; i < 3; ++i)
			if (ui.Selectable(modes[i], static_cast<int>(edited.mode) == i)) {
				edited.mode = static_cast<TransformAnimationComponent::Mode>(i);
				changed |= history.SetTransformAnimation(id, edited);
			}
		ImGui::EndCombo();
	}
	const char *interpolations[]{"Linear", "Smooth", "Step"};
	if (ImGui::BeginCombo(
	        "Interpolation",
	        static_cast<int>(edited.interpolation) >= 0 &&
	                static_cast<int>(edited.interpolation) < 3
	            ? interpolations[static_cast<int>(edited.interpolation)]
	            : "Invalid")) {
		for (int i = 0; i < 3; ++i)
			if (ui.Selectable(interpolations[i],
			                  static_cast<int>(edited.interpolation) == i)) {
				edited.interpolation =
				    static_cast<TransformAnimationComponent::Interpolation>(i);
				changed |= history.SetTransformAnimation(id, edited);
			}
		ImGui::EndCombo();
	}
	apply(ImGui::DragFloat("Animation Speed", &edited.speed, .01f, .01f,
	                       TransformAnimationComponent::kMaximumSpeed, "%.2f",
	                       ImGuiSliderFlags_AlwaysClamp));
	if (ImGui::Checkbox("Play On Start", &edited.playOnStart))
		changed |= history.SetTransformAnimation(id, edited);
	std::size_t remove = edited.keys.size();

	if (ImGui::BeginChild("AnimationKeys", {0, 300}, ImGuiChildFlags_Borders)) {
		for (std::size_t i = 0; i < edited.keys.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			auto &key = edited.keys[i];
			ImGui::SeparatorText(std::format("Key {}", i).c_str());
			ImGui::BeginDisabled(i == 0);
			const float minimum =
			    i ? std::nextafter(edited.keys[i - 1].seconds,
			                       std::numeric_limits<float>::infinity())
			      : 0;
			const float maximum =
			    i + 1 < edited.keys.size()
			        ? std::nextafter(edited.keys[i + 1].seconds,
			                         -std::numeric_limits<float>::infinity())
			        : TransformAnimationComponent::kMaximumSeconds;
			apply(ImGui::DragFloat("Seconds", &key.seconds, .01f, minimum,
			                       maximum, "%.4f",
			                       ImGuiSliderFlags_AlwaysClamp));
			ImGui::EndDisabled();
			apply(ImGui::DragFloat3(
			    "Position Offset", &key.offset.translation.x, .05f,
			    -TransformAnimationComponent::kMaximumOffset,
			    TransformAnimationComponent::kMaximumOffset, "%.3f",
			    ImGuiSliderFlags_AlwaysClamp));
			auto radians = TransformUtils::RotationToEuler(key.offset.rotation);
			float angles[]{DirectX::XMConvertToDegrees(radians.x),
			               DirectX::XMConvertToDegrees(radians.y),
			               DirectX::XMConvertToDegrees(radians.z)};
			const bool rotationChanged =
			    ImGui::DragFloat3("Rotation Offset", angles, .5f, -360, 360,
			                      "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (rotationChanged)
				key.offset.rotation = TransformUtils::RotationFromEuler(
				    {DirectX::XMConvertToRadians(angles[0]),
				     DirectX::XMConvertToRadians(angles[1]),
				     DirectX::XMConvertToRadians(angles[2])});
			apply(rotationChanged);
			apply(ImGui::DragFloat3("Scale Multiplier", &key.offset.scale.x,
			                        .01f,
			                        -TransformAnimationComponent::kMaximumScale,
			                        TransformAnimationComponent::kMaximumScale,
			                        "%.3f", ImGuiSliderFlags_AlwaysClamp));
			ImGui::BeginDisabled(i == 0 || edited.keys.size() <= 2);
			if (ui.Button("Remove Key"))
				remove = i;
			ImGui::EndDisabled();
			ImGui::PopID();
		}
	}

	ImGui::EndChild();

	if (remove < edited.keys.size()) {
		edited.keys.erase(edited.keys.begin() +
		                  static_cast<std::ptrdiff_t>(remove));
		changed |= history.SetTransformAnimation(id, edited);
	}

	ui.BeginDisabled(
	    edited.keys.size() >= TransformAnimationComponent::kMaximumKeys ||
	    edited.GetDuration() >= TransformAnimationComponent::kMaximumSeconds);
	if (ui.Button("Add Key")) {
		auto key = edited.keys.back();
		key.seconds = (std::min)(TransformAnimationComponent::kMaximumSeconds,
		                         key.seconds + 1);
		edited.keys.push_back(key);
		changed |= history.SetTransformAnimation(id, edited);
	}

	ui.EndDisabled();
	if (ui.Button("Reset Transform Animation"))
		changed |=
		    history.SetTransformAnimation(id, TransformAnimationComponent{});
	if (ui.Button("Remove Transform Animation"))
		changed |= history.SetTransformAnimation(id, std::nullopt);
	ImGui::PopID();
	ImGui::PopID();
	return changed;
}

void TransformAnimationInspector::DrawReadOnly(
    const ImGuiSystem &ui, const SceneObject &object) const {
	if (!object.transformAnimation ||
	    !ui.CollapsingHeader("Transform Animation"))
		return;
	const auto &animation = *object.transformAnimation;
	ui.Text(std::format("Keys: {}", animation.keys.size()));
	ui.Text(std::format("Duration: {:.3f} s", animation.GetDuration()));
	ui.Text(std::format("Speed: {:.2f}", animation.speed));
}
