#pragma once

#include "scene/SceneObjectId.h"
#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <utility>
#include <vector>

/// <summary>
/// シーン物体のプロパティを、秒単位のキーで指定する
/// </summary>
struct AnimationClipKey {
	float seconds = 0;
	std::array<float, 4> value{};
	bool operator==(const AnimationClipKey &) const = default;
};

struct AnimationClipTrack {
	enum class Property { POSITION, OPACITY, TRANSLATION, ROTATION, SCALE };
	enum class Interpolation { LINEAR, SMOOTH, STEP };

	SceneObjectId target = 0;
	Property property = Property::POSITION;
	Interpolation interpolation = Interpolation::LINEAR;
	std::vector<AnimationClipKey> keys{{0, {}}, {1, {}}};

	/// <summary>
	/// 検証済みのキーを補間する。先頭と末尾の範囲外では端の値を保持する
	/// </summary>
	bool IsTransform() const {
		return property == Property::TRANSLATION || property == Property::ROTATION ||
		       property == Property::SCALE;
	}

	std::array<float, 4> Sample(double seconds) const {
		auto next = std::ranges::upper_bound(keys, seconds, {},
		                                     &AnimationClipKey::seconds);
		if (next == keys.begin())
			return keys.front().value;
		const auto &previous = *(next - 1);
		if (next == keys.end() || interpolation == Interpolation::STEP)
			return previous.value;

		float amount = static_cast<float>((seconds - previous.seconds) /
		                                  (next->seconds - previous.seconds));
		if (interpolation == Interpolation::SMOOTH)
			amount = amount * amount * (3 - 2 * amount);
		if (property == Property::ROTATION) {
			using namespace DirectX;
			const auto &a = previous.value;
			const auto &b = next->value;
			XMFLOAT4 value;
			XMStoreFloat4(&value, XMQuaternionNormalize(XMQuaternionSlerp(
			    XMVectorSet(a[0], a[1], a[2], a[3]),
			    XMVectorSet(b[0], b[1], b[2], b[3]), amount)));
			return {value.x, value.y, value.z, value.w};
		}
		return {std::lerp(previous.value[0], next->value[0], amount),
		        std::lerp(previous.value[1], next->value[1], amount),
		        std::lerp(previous.value[2], next->value[2], amount),
		        std::lerp(previous.value[3], next->value[3], amount)};
	}

	bool operator==(const AnimationClipTrack &) const = default;
};

/// <summary>
/// TransformとUIを同じ時刻で更新し、マーカーや末尾でTrigger Actionsを発火する
/// </summary>
struct AnimationClipComponent {
	static constexpr std::size_t kMaximumTracks = 32;
	static constexpr std::size_t kMaximumKeys = 64;
	static constexpr float kMaximumSeconds = 3600;
	float duration = 1;
	bool playOnStart = false;
	bool loop = false;
	std::vector<AnimationClipTrack> tracks;
	// アセットを使うプレイヤーでは、トラックの対象枠をシーン内の物体に割り当てる。
	bool usesAsset = false;
	std::filesystem::path asset;
	std::vector<std::pair<SceneObjectId, SceneObjectId>> bindings;

	bool IsValid() const {
		if (usesAsset) {
			if (!tracks.empty() || bindings.size() > kMaximumTracks ||
			    asset.native().find(L'\0') != std::wstring::npos)
				return false;

			std::array<bool, kMaximumTracks + 1> used{};
			for (const auto &[slot, target] : bindings) {
				(void)target;
				if (slot == 0 || slot > kMaximumTracks || used[slot])
					return false;
				used[slot] = true;
			}
			return true;
		}

		if (!std::isfinite(duration) || duration <= 0 ||
		    duration > kMaximumSeconds || tracks.size() > kMaximumTracks)
			return false;

		for (std::size_t i = 0; i < tracks.size(); ++i) {
			const auto &track = tracks[i];
			if ((track.property != AnimationClipTrack::Property::POSITION &&
			     track.property != AnimationClipTrack::Property::OPACITY &&
			     !track.IsTransform()) ||
			    (track.interpolation != AnimationClipTrack::Interpolation::LINEAR &&
			     track.interpolation != AnimationClipTrack::Interpolation::SMOOTH &&
			     track.interpolation != AnimationClipTrack::Interpolation::STEP) ||
			    track.keys.empty() || track.keys.size() > kMaximumKeys)
				return false;

			for (std::size_t j = 0; j < i; ++j)
				if (track.target != 0 && tracks[j].target == track.target &&
				    tracks[j].property == track.property)
					return false;

			float previous = -1;
			const AnimationClipKey *previousKey = nullptr;
			for (const auto &key : track.keys) {
				if (!std::isfinite(key.seconds) || key.seconds < 0 ||
				    key.seconds > duration || key.seconds <= previous ||
				    !std::ranges::all_of(key.value, [](float value) {
					    return std::isfinite(value) && std::abs(value) <= 100000;
				    }) ||
				    (track.property == AnimationClipTrack::Property::OPACITY &&
				     (key.value[0] < 0 || key.value[0] > 1)))
					return false;
				if (track.property == AnimationClipTrack::Property::ROTATION) {
					const auto &v = key.value;
					const float squared = v[0] * v[0] + v[1] * v[1] +
					                      v[2] * v[2] + v[3] * v[3];
					if (std::abs(squared - 1) > .001f)
						return false;
				}
				if (track.property == AnimationClipTrack::Property::SCALE) {
					for (std::size_t axis = 0; axis < 3; ++axis) {
						const auto value = key.value[axis];
						if (std::abs(value) < .001f || std::abs(value) > 1000 ||
						    (previousKey &&
						     track.interpolation != AnimationClipTrack::Interpolation::STEP &&
						     std::signbit(value) != std::signbit(previousKey->value[axis])))
							return false;
					}
				}
				previous = key.seconds;
				previousKey = &key;
			}
		}

		return true;
	}

	bool operator==(const AnimationClipComponent &) const = default;
};
