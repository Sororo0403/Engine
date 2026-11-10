#pragma once

#include "scene/SceneObjectId.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

/// <summary>
/// UIの位置または透明度を、秒単位のキーで指定する
/// </summary>
struct UITimelineKey {
	float seconds = 0;
	std::array<float, 2> value{};
	bool operator==(const UITimelineKey &) const = default;
};

struct UITimelineTrack {
	enum class Property { POSITION, OPACITY };
	enum class Interpolation { LINEAR, SMOOTH, STEP };

	SceneObjectId target = 0;
	Property property = Property::POSITION;
	Interpolation interpolation = Interpolation::LINEAR;
	std::vector<UITimelineKey> keys{{0, {}}, {1, {}}};

	/// <summary>
	/// 検証済みのキーを補間する。先頭と末尾の範囲外では端の値を保持する
	/// </summary>
	std::array<float, 2> Sample(float seconds) const {
		auto next = std::ranges::upper_bound(keys, seconds, {},
		                                     &UITimelineKey::seconds);
		if (next == keys.begin())
			return keys.front().value;
		const auto &previous = *(next - 1);
		if (next == keys.end() || interpolation == Interpolation::STEP)
			return previous.value;

		float amount = (seconds - previous.seconds) /
		               (next->seconds - previous.seconds);
		if (interpolation == Interpolation::SMOOTH)
			amount = amount * amount * (3 - 2 * amount);
		return {std::lerp(previous.value[0], next->value[0], amount),
		        std::lerp(previous.value[1], next->value[1], amount)};
	}

	bool operator==(const UITimelineTrack &) const = default;
};

/// <summary>
/// 複数のUIを同じ時刻で更新し、末尾でTrigger Actionsを発火する
/// </summary>
struct UITimelineComponent {
	static constexpr std::size_t kMaximumTracks = 32;
	static constexpr std::size_t kMaximumKeys = 64;
	static constexpr float kMaximumSeconds = 3600;
	float duration = 1;
	bool playOnStart = false;
	std::vector<UITimelineTrack> tracks;

	bool IsValid() const {
		if (!std::isfinite(duration) || duration <= 0 ||
		    duration > kMaximumSeconds || tracks.size() > kMaximumTracks)
			return false;

		for (std::size_t i = 0; i < tracks.size(); ++i) {
			const auto &track = tracks[i];
			if ((track.property != UITimelineTrack::Property::POSITION &&
			     track.property != UITimelineTrack::Property::OPACITY) ||
			    (track.interpolation != UITimelineTrack::Interpolation::LINEAR &&
			     track.interpolation != UITimelineTrack::Interpolation::SMOOTH &&
			     track.interpolation != UITimelineTrack::Interpolation::STEP) ||
			    track.keys.empty() || track.keys.size() > kMaximumKeys)
				return false;

			for (std::size_t j = 0; j < i; ++j)
				if (track.target != 0 && tracks[j].target == track.target &&
				    tracks[j].property == track.property)
					return false;

			float previous = -1;
			for (const auto &key : track.keys) {
				if (!std::isfinite(key.seconds) || key.seconds < 0 ||
				    key.seconds > duration || key.seconds <= previous ||
				    !std::isfinite(key.value[0]) || !std::isfinite(key.value[1]) ||
				    std::abs(key.value[0]) > 100000 ||
				    std::abs(key.value[1]) > 100000 ||
				    (track.property == UITimelineTrack::Property::OPACITY &&
				     (key.value[0] < 0 || key.value[0] > 1)))
					return false;
				previous = key.seconds;
			}
		}

		return true;
	}

	bool operator==(const UITimelineComponent &) const = default;
};
