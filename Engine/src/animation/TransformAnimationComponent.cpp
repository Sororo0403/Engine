#include "animation/TransformAnimationComponent.h"
#include "math/TransformUtils.h"
#include <algorithm>
#include <cmath>

bool TransformAnimationKey::operator==(
    const TransformAnimationKey &other) const {
	return seconds == other.seconds &&
	       TransformUtils::AreEqual(offset, other.offset);
}

bool TransformAnimationComponent::IsValid() const {
	if (keys.size() < 2 || keys.size() > kMaximumKeys ||
	    keys.front().seconds != 0 || !std::isfinite(speed) || speed <= 0 ||
	    speed > kMaximumSpeed ||
	    (mode != Mode::ONCE && mode != Mode::LOOP && mode != Mode::PING_PONG) ||
	    (interpolation != Interpolation::LINEAR &&
	     interpolation != Interpolation::SMOOTH &&
	     interpolation != Interpolation::STEP))
		return false;
	float previous = -1;
	for (const auto &key : keys) {
		if (!std::isfinite(key.seconds) || key.seconds <= previous ||
		    key.seconds > kMaximumSeconds ||
		    !TransformUtils::IsValid(key.offset))
			return false;
		for (float value : {key.offset.translation.x, key.offset.translation.y,
		                    key.offset.translation.z})
			if (std::abs(value) > kMaximumOffset)
				return false;
		const auto &a = key.offset.scale, &b = keys.front().offset.scale;
		for (float value : {a.x, a.y, a.z})
			if (std::abs(value) < kMinimumScale ||
			    std::abs(value) > kMaximumScale)
				return false;
		if (a.x * b.x <= 0 || a.y * b.y <= 0 || a.z * b.z <= 0)
			return false;
		previous = key.seconds;
	}
	return GetDuration() >= .001;
}

double TransformAnimationComponent::GetDuration() const {
	return keys.empty() ? 0 : keys.back().seconds;
}

double TransformAnimationComponent::GetClipTime(double seconds) const {
	const auto duration = GetDuration();
	if (!std::isfinite(seconds) || seconds < 0 || !std::isfinite(duration) ||
	    duration <= 0)
		return 0;
	if (mode == Mode::ONCE)
		return (std::min)(seconds, duration);
	const double time =
	    std::fmod(seconds, mode == Mode::PING_PONG ? duration * 2 : duration);
	return mode == Mode::PING_PONG && time > duration ? duration * 2 - time
	                                                  : time;
}

bool TransformAnimationComponent::Sample(double seconds,
                                         Transform &offset) const {
	if (!IsValid() || !std::isfinite(seconds) || seconds < 0)
		return false;
	const auto time = GetClipTime(seconds);
	auto next = std::upper_bound(
	    keys.begin(), keys.end(), time,
	    [](double t, const auto &key) { return t < key.seconds; });
	Transform result;
	if (next == keys.end())
		result = keys.back().offset;
	else if (next == keys.begin())
		result = next->offset;
	else {
		const auto &a = *(next - 1), &b = *next;
		float t =
		    static_cast<float>((time - a.seconds) / (b.seconds - a.seconds));
		if (interpolation == Interpolation::STEP)
			t = 0;
		else if (interpolation == Interpolation::SMOOTH)
			t = t * t * (3 - 2 * t);
		using namespace DirectX;
		XMStoreFloat3(&result.translation,
		              XMVectorLerp(XMLoadFloat3(&a.offset.translation),
		                           XMLoadFloat3(&b.offset.translation), t));
		XMStoreFloat3(&result.scale,
		              XMVectorLerp(XMLoadFloat3(&a.offset.scale),
		                           XMLoadFloat3(&b.offset.scale), t));
		XMStoreFloat4(&result.rotation,
		              XMQuaternionNormalize(XMQuaternionSlerp(
		                  XMLoadFloat4(&a.offset.rotation),
		                  XMLoadFloat4(&b.offset.rotation), t)));
	}
	if (!TransformUtils::IsValid(result))
		return false;
	offset = result;
	return true;
}
