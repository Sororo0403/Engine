#include "audio/AudioSettings.h"

#include "math/MathUtils.h"
#include <cmath>

bool AudioListenerPose::IsValid() const {
	const double front = double(forward.x) * forward.x +
	                     double(forward.y) * forward.y +
	                     double(forward.z) * forward.z;
	const double top =
	    double(up.x) * up.x + double(up.y) * up.y + double(up.z) * up.z;
	const double dot = double(forward.x) * up.x + double(forward.y) * up.y +
	                   double(forward.z) * up.z;
	return MathUtils::IsFinite(position) && MathUtils::IsFinite(forward) &&
	       MathUtils::IsFinite(up) && std::abs(front - 1) <= .0001 &&
	       std::abs(top - 1) <= .0001 && std::abs(dot) <= .0001;
}

bool AudioEmitterSettings::IsValid() const {
	if (!MathUtils::IsFinite(position) || !std::isfinite(minDistance) ||
	    !std::isfinite(maxDistance) || minDistance < 0 || maxDistance < .001f ||
	    maxDistance > kMaximumDistance || minDistance >= maxDistance) {
		return false;
	}

	const auto normalized = minDistance / maxDistance;
	return minDistance == 0 || (normalized > 0 && normalized < 1);
}

bool AudioPlaybackSettings::IsValid() const {
	return std::isfinite(volume) && volume >= 0 && volume <= kMaximumVolume &&
	       (!spatial || emitter.IsValid());
}
