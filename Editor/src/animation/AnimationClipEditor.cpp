#include "animation/AnimationClipEditor.h"

#include "animation/AnimationClipAsset.h"
#include "graphics/ui/ImGuiSystem.h"
#include "math/TransformUtils.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include <algorithm>
#include <exception>
#include <format>
#include <imgui.h>

/// <summary>
/// タイムラインの時刻、対象、キーフレームを一時データ上で編集する
/// </summary>
void AnimationClipEditor::DrawKeys(const Scene *scene, AnimationClipComponent &timeline,
                        float &playhead) {
	if (scene) {
		ImGui::Checkbox("Play on Start", &timeline.playOnStart);
		ImGui::SameLine();
		ImGui::Checkbox("Loop", &timeline.loop);
	}
	ImGui::DragFloat("Duration (s)", &timeline.duration, .05f, .01f,
	                 AnimationClipComponent::kMaximumSeconds, "%.3f",
	                 ImGuiSliderFlags_AlwaysClamp);
	const auto pastEnd = [&](const AnimationClipKey &key) {
		return key.seconds > timeline.duration;
	};
	std::size_t outsideCount = 0;
	for (const auto &track : timeline.tracks) {
		outsideCount += static_cast<std::size_t>(std::ranges::count_if(track.keys, pastEnd));
	}
	if (outsideCount != 0) {
		ImGui::TextWrapped("%zu key(s) are past the clip end. Move or remove them, or extend the duration.",
		                   outsideCount);
	}
	playhead = std::clamp(playhead, 0.f, timeline.duration);
	ImGui::SliderFloat("Playhead", &playhead, 0.f, timeline.duration, "%.3f s");

	std::optional<std::size_t> removedTrack;
	for (std::size_t i = 0; i < timeline.tracks.size(); ++i) {
		auto &track = timeline.tracks[i];
		ImGui::PushID(static_cast<int>(i));
		const auto *target = scene ? scene->FindObject(track.target) : nullptr;
		auto title = std::format("Track {}  /  {}", i + 1,
		                               scene ? (target ? target->name : "(No target)")
		                               : std::format("Binding {}", track.target));
		if (const auto count = std::ranges::count_if(track.keys, pastEnd); count != 0) {
			title += std::format("  ({} past end)", count);
		}
		if (ImGui::TreeNodeEx("Track", ImGuiTreeNodeFlags_DefaultOpen, "%s",
		                     title.c_str())) {
			if (scene && ImGui::BeginCombo("Target", target ? target->name.c_str() : "(None)")) {
				for (const auto &[id, object] : scene->GetObjects()) {
					if (!object.CanAnimate(track))
						continue;
					const auto label = std::format("{}##timeline-target-{}", object.name, id);
					if (ImGui::Selectable(label.c_str(), id == track.target))
						track.target = id;
				}
				ImGui::EndCombo();
			}

			if (!scene) {
				int slot = static_cast<int>(track.target);
				if (ImGui::DragInt("Binding Slot", &slot, 1, 1,
				                   static_cast<int>(AnimationClipComponent::kMaximumTracks),
				                   "%d", ImGuiSliderFlags_AlwaysClamp))
					track.target = static_cast<SceneObjectId>(slot);
			}

			int property = static_cast<int>(track.property);
			if (ImGui::Combo("Property", &property,
			                 "UI Position\0UI Opacity\0Translation\0Rotation\0Scale\0UI Size Delta\0UI Rotation\0")) {
				track.property = static_cast<AnimationClipTrack::Property>(property);
				if (track.property != AnimationClipTrack::Property::POSITION &&
				    track.interpolation == AnimationClipTrack::Interpolation::EASE_OUT_BACK)
					track.interpolation = AnimationClipTrack::Interpolation::LINEAR;
				const auto *selected = scene ? scene->FindObject(track.target) : nullptr;
				if (selected && !selected->CanAnimate(track))
					track.target = 0;
				for (auto &key : track.keys) {
					if (track.property == AnimationClipTrack::Property::OPACITY)
						key.value = {std::clamp(key.value[0], 0.f, 1.f), 0};
					else if (track.property == AnimationClipTrack::Property::ROTATION)
						key.value = {0, 0, 0, 1};
					else if (track.property == AnimationClipTrack::Property::SCALE)
						key.value = {1, 1, 1, 0};
					else if (track.property == AnimationClipTrack::Property::UI_ROTATION)
						key.value = {};
				}
			}
			const bool opacity = track.property == AnimationClipTrack::Property::OPACITY;
			const bool rotation = track.property == AnimationClipTrack::Property::ROTATION;
			const bool uiRotation = track.property == AnimationClipTrack::Property::UI_ROTATION;
			int interpolation = static_cast<int>(track.interpolation);
			const char *interpolations = track.property == AnimationClipTrack::Property::POSITION
			    ? "Linear\0Smooth\0Step\0Ease Out Cubic\0Ease Out Back\0"
			    : "Linear\0Smooth\0Step\0Ease Out Cubic\0";
			if (ImGui::Combo("Interpolation", &interpolation, interpolations))
				track.interpolation = static_cast<AnimationClipTrack::Interpolation>(interpolation);

			// キーの印をクリックすると、その時刻へプレイヘッドを移す。
			const auto origin = ImGui::GetCursorScreenPos();
			const float width = (std::max)(ImGui::GetContentRegionAvail().x, 1.f);
			ImGui::InvisibleButton("Key Strip", {width, 19});
			if (ImGui::IsItemClicked()) {
				const float fraction = (ImGui::GetMousePos().x - origin.x) / width;
				const float time = std::clamp(fraction, 0.f, 1.f) * timeline.duration;
				const auto closest = std::ranges::min_element(track.keys, {},
				    [&](const auto &key) { return std::abs(key.seconds - time); });
				playhead = closest != track.keys.end() &&
				                   !pastEnd(*closest) &&
				                   std::abs(closest->seconds - time) <= 7 * timeline.duration / width
				               ? closest->seconds
				               : time;
			}
			auto *draw = ImGui::GetWindowDrawList();
			draw->AddLine({origin.x, origin.y + 9}, {origin.x + width, origin.y + 9},
			              ImGui::GetColorU32(ImGuiCol_Border));
			for (const auto &key : track.keys) {
				const float x = origin.x + width *
				    std::clamp(key.seconds / timeline.duration, 0.f, 1.f);
				draw->AddCircleFilled({x, origin.y + 9}, 4,
				                      ImGui::GetColorU32(ImGuiCol_SliderGrab));
			}
			const float x = origin.x + width * playhead / timeline.duration;
			draw->AddLine({x, origin.y}, {x, origin.y + 18},
			              ImGui::GetColorU32(ImGuiCol_Text));

			std::optional<std::size_t> removedKey;
			for (std::size_t k = 0; k < track.keys.size(); ++k) {
				auto &key = track.keys[k];
				ImGui::PushID(static_cast<int>(k));
				ImGui::SetNextItemWidth(105);
				ImGui::DragFloat("Time", &key.seconds, .02f, 0.f, timeline.duration,
				                 "%.3f", ImGuiSliderFlags_AlwaysClamp);
				if (pastEnd(key)) {
					ImGui::SetItemTooltip("This key is past the clip end (%.3f s).", timeline.duration);
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(160);
				if (opacity)
					ImGui::SliderFloat("Alpha", &key.value[0], 0.f, 1.f,
					                   "%.2f", ImGuiSliderFlags_AlwaysClamp);
				else if (uiRotation)
					ImGui::DragFloat("Angle (deg)", &key.value[0], .5f, -36000, 36000,
					                 "%.2f", ImGuiSliderFlags_AlwaysClamp);
				else if (rotation) {
					auto radians = TransformUtils::RotationToEuler(
					    {key.value[0], key.value[1], key.value[2], key.value[3]});
					float degrees[]{DirectX::XMConvertToDegrees(radians.x),
					                DirectX::XMConvertToDegrees(radians.y),
					                DirectX::XMConvertToDegrees(radians.z)};
					if (ImGui::DragFloat3("Rotation (deg)", degrees, .5f)) {
						const auto q = TransformUtils::RotationFromEuler(
						    {DirectX::XMConvertToRadians(degrees[0]),
						     DirectX::XMConvertToRadians(degrees[1]),
						     DirectX::XMConvertToRadians(degrees[2])});
						key.value = {q.x, q.y, q.z, q.w};
					}
				} else if (track.IsTransform()) {
					const float limit = track.property == AnimationClipTrack::Property::SCALE
					                        ? 1000.f : 100000.f;
					ImGui::DragFloat3("XYZ", key.value.data(), .05f, -limit, limit,
					                  "%.3f", ImGuiSliderFlags_AlwaysClamp);
				} else
					ImGui::DragFloat2(track.property == AnimationClipTrack::Property::SIZE_DELTA
					                      ? "Width / Height" : "XY",
					                  key.value.data(), 1.f, -100000, 100000,
					                  "%.1f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SameLine();
				ImGui::BeginDisabled(track.keys.size() == 1);
				if (ImGui::SmallButton("Remove Key"))
					removedKey = k;
				ImGui::EndDisabled();
				ImGui::PopID();
			}
			if (removedKey)
				track.keys.erase(track.keys.begin() + static_cast<std::ptrdiff_t>(*removedKey));
			std::ranges::sort(track.keys, {}, &AnimationClipKey::seconds);
			const bool hasKey = std::ranges::any_of(track.keys, [&](const auto &key) {
				return std::abs(key.seconds - playhead) < .0001f;
			});
			ImGui::BeginDisabled(hasKey || track.keys.size() >= AnimationClipComponent::kMaximumKeys);
			if (ImGui::Button("Add Key at Playhead")) {
				track.keys.push_back({playhead, track.Sample(playhead)});
				std::ranges::sort(track.keys, {}, &AnimationClipKey::seconds);
			}
			ImGui::EndDisabled();
			const auto sample = track.Sample(playhead);
			if (opacity)
				ImGui::Text("At playhead: %.2f", sample[0]);
			else if (uiRotation)
				ImGui::Text("At playhead: %.2f deg", sample[0]);
			else if (rotation) {
				const auto euler = TransformUtils::RotationToEuler(
				    {sample[0], sample[1], sample[2], sample[3]});
				ImGui::Text("At playhead: %.1f, %.1f, %.1f deg",
				            DirectX::XMConvertToDegrees(euler.x),
				            DirectX::XMConvertToDegrees(euler.y),
				            DirectX::XMConvertToDegrees(euler.z));
			} else if (track.IsTransform())
				ImGui::Text("At playhead: %.3f, %.3f, %.3f", sample[0], sample[1], sample[2]);
			else
				ImGui::Text("At playhead: %.1f, %.1f", sample[0], sample[1]);
			if (ImGui::Button("Remove Track"))
				removedTrack = i;
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (removedTrack)
		timeline.tracks.erase(timeline.tracks.begin() + static_cast<std::ptrdiff_t>(*removedTrack));

	ImGui::BeginDisabled(timeline.tracks.size() >= AnimationClipComponent::kMaximumTracks);
	if (ImGui::Button("Add Track")) {
		auto &track = timeline.tracks.emplace_back();
		if (!scene)
			track.target = 1;
		track.keys.back().seconds = timeline.duration;
	}
	ImGui::EndDisabled();
}


void AnimationClipEditor::ShowCreate(const std::filesystem::path &directory) {
	directory_ = directory;
	path_.clear();
	markerTimes_.clear();
	name_ = "New Animation Clip";
	clip_ = {};
	original_ = {};
	error_.clear();
	playhead_ = 0;
	openRequested_ = true;
}

bool AnimationClipEditor::ShowEdit(const std::filesystem::path &path,
                                  const Scene &editingScene,
                                  std::string &error) {
	AnimationClipComponent loaded;
	if (!AnimationClipAsset::Load(path, loaded, error))
		return false;

	markerTimes_.clear();
	for (const auto &[id, object] : editingScene.GetObjects()) {
		if (!object.triggerActions || !object.animationClip || !object.animationClip->usesAsset) {
			continue;
		}
		const auto &reference = object.animationClip->asset;
		if (reference.empty() || (reference.has_root_path() && !reference.is_absolute()) ||
		    reference.native().find(L'\0') != std::wstring::npos ||
		    !PathUtils::IsSameFile(path, reference)) {
			continue;
		}
		for (const auto &action : object.triggerActions->actions) {
			if (action.event == TriggerAction::Event::ANIMATION_CLIP_MARKER) {
				markerTimes_.push_back(action.clipTime);
			}
		}
	}

	original_ = loaded;
	clip_ = std::move(loaded);
	path_ = path;
	error_.clear();
	playhead_ = 0;
	openRequested_ = true;
	return true;
}

AnimationClipEditor::Result AnimationClipEditor::Draw(const ImGuiSystem &ui) {
	constexpr auto title = "Animation Clip Asset";
	if (openRequested_) {
		ui.OpenPopup(title);
		openRequested_ = false;
	}
	const auto viewport = ImGui::GetMainViewport()->WorkSize;
	const ImVec2 maximum{(std::max)(1.f, viewport.x - 24),
	                     (std::max)(1.f, viewport.y - 24)};
	ImGui::SetNextWindowSize({(std::min)(700.f, maximum.x),
	                         (std::min)(540.f, maximum.y)}, ImGuiCond_Appearing);
	ImGui::SetNextWindowSizeConstraints(
	    {(std::min)(420.f, maximum.x), (std::min)(300.f, maximum.y)}, maximum);
	if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoSavedSettings))
		return Result::NONE;

	if (path_.empty())
		ui.InputText("Name", name_);
	else
		ui.Text(PathUtils::ToUtf8(path_.filename()));

	const float footerRows = markerTimes_.empty() ? 3.f : 5.f;
	ImGui::BeginChild("Clip Tracks", {0, -ImGui::GetFrameHeightWithSpacing() * footerRows});
	DrawKeys(nullptr, clip_, playhead_);
	ImGui::EndChild();
	const auto affectedMarkers = std::ranges::count_if(markerTimes_, [&](float time) {
		return time > clip_.duration && time <= original_.duration;
	});
	if (affectedMarkers != 0) {
		const auto warning = std::format(
		    "Shortening this clip puts {} marker(s) past the end in the current scene.", affectedMarkers);
		ImGui::TextWrapped("%s", warning.c_str());
	}
	if (!error_.empty())
		ui.Text(error_);
	Result result = Result::NONE;
	if (ui.Button("Save Clip")) {
		try {
			auto destination = path_;
			if (destination.empty()) {
				auto filename = PathUtils::FromUtf8(name_);
				if (!PathUtils::IsValidFileName(filename))
					throw std::runtime_error("Enter a valid clip filename.");
				if (!AnimationClipAsset::IsPath(filename))
					filename += L".clip.json";
				destination = directory_ / filename;
			} else {
				AnimationClipComponent current;
				if (!AnimationClipAsset::Load(destination, current, error_) ||
				    current != original_)
					throw std::runtime_error("The clip changed on disk. Reopen it before saving.");
			}

			if (AnimationClipAsset::Save(destination, clip_, error_, !path_.empty())) {
				path_ = std::move(destination);
				result = Result::SAVED;
			}
		} catch (const std::exception &error) {
			error_ = error.what();
		}
	}
	ui.SameLine();
	if (ui.Button("Cancel"))
		result = Result::CANCELLED;
	if (result != Result::NONE)
		ui.CloseCurrentPopup();
	ui.EndPopup();
	return result;
}
