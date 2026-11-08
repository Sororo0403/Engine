#include "gameplay/MotionPathComponent.h"
#include <algorithm>
#include <cmath>

bool MotionPathComponent::IsValid() const {
	if (points.size() < 2 || points.size() > kMaximumPoints ||
	    points.front() != std::array<float, 3>{0, 0, 0} ||
	    !std::isfinite(segmentSeconds) ||
	    segmentSeconds < kMinimumSegmentSeconds ||
	    segmentSeconds > kMaximumSegmentSeconds ||
	    !std::isfinite(waypointWaitSeconds) || waypointWaitSeconds < 0 ||
	    waypointWaitSeconds > kMaximumWaypointWaitSeconds ||
	    (mode != Mode::ONCE && mode != Mode::LOOP && mode != Mode::PING_PONG))
		return false;
	for (const auto &point : points)
		for (float coordinate : point) {
			if (!std::isfinite(coordinate) ||
			    std::abs(coordinate) > kMaximumOffset)
				return false;
		}
	return true;
}

double MotionPathComponent::GetDuration() const {
	if (!IsValid())
		return 0;
	const auto segments =
	    mode == Mode::LOOP ? points.size() : points.size() - 1;
	return (double(segmentSeconds) + waypointWaitSeconds) * double(segments) *
	       (mode == Mode::PING_PONG ? 2.0 : 1.0);
}

double MotionPathComponent::GetTimeToBoundary(double seconds,
                                              bool reverse) const {
	const double travel = segmentSeconds;
	const double interval = travel + waypointWaitSeconds;
	const double phase = std::fmod(seconds, interval);
	constexpr double tolerance = 1e-10;
	if (reverse) {
		if (phase <= tolerance)
			return waypointWaitSeconds > tolerance ? waypointWaitSeconds
			                                       : travel;
		return phase > travel + tolerance ? phase - travel : phase;
	}
	if (phase < travel - tolerance)
		return travel - phase;
	return interval - phase;
}

bool MotionPathComponent::Sample(double seconds,
                                 DirectX::XMFLOAT3 &offset) const {
	if (!IsValid() || !std::isfinite(seconds) || seconds < 0)
		return false;
	const double duration = GetDuration();
	double time = mode == Mode::ONCE ? (std::min)(seconds, duration)
	                                 : std::fmod(seconds, duration);
	const auto last = points.size() - 1;
	const double interval = double(segmentSeconds) + waypointWaitSeconds;
	const auto lastLeg = mode == Mode::LOOP        ? last
	                     : mode == Mode::PING_PONG ? 2 * last - 1
	                                               : last - 1;
	const auto leg =
	    (std::min)(static_cast<std::size_t>(time / interval), lastLeg);
	const bool returning = mode == Mode::PING_PONG && leg >= last;
	const auto index = returning ? 2 * last - leg : leg;
	const auto next = returning ? index - 1 : (index + 1) % points.size();
	double ratio =
	    (std::clamp)((time - double(leg) * interval) / segmentSeconds, 0.0,
	                 1.0);
	if (smooth)
		ratio = ratio * ratio * (3.0 - 2.0 * ratio);
	const auto &a = points[index], &b = points[next];
	offset = {float(std::lerp(double(a[0]), double(b[0]), ratio)),
	          float(std::lerp(double(a[1]), double(b[1]), ratio)),
	          float(std::lerp(double(a[2]), double(b[2]), ratio))};
	return true;
}
