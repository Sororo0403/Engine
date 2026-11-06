#include "gameplay/MotionPathComponent.h"
#include <algorithm>
#include <cmath>

bool MotionPathComponent::IsValid() const {
	if (points.size() < 2 || points.size() > kMaximumPoints ||
	    points.front() != std::array<float, 3>{0, 0, 0} ||
	    !std::isfinite(segmentSeconds) ||
	    segmentSeconds < kMinimumSegmentSeconds ||
	    segmentSeconds > kMaximumSegmentSeconds ||
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
	return double(segmentSeconds) * double(segments) *
	       (mode == Mode::PING_PONG ? 2.0 : 1.0);
}

bool MotionPathComponent::Sample(double seconds,
                                 DirectX::XMFLOAT3 &offset) const {
	if (!IsValid() || !std::isfinite(seconds) || seconds < 0)
		return false;
	const double duration = GetDuration();
	double time = mode == Mode::ONCE ? (std::min)(seconds, duration)
	                                 : std::fmod(seconds, duration);
	const auto last = points.size() - 1;
	if (mode == Mode::PING_PONG && time > duration / 2)
		time = duration - time;
	const double position = time / double(segmentSeconds);
	const auto index = (std::min)(static_cast<std::size_t>(position), last);
	const auto next =
	    index == last ? (mode == Mode::LOOP ? 0 : last) : index + 1;
	double ratio = position - double(index);
	if (smooth)
		ratio = ratio * ratio * (3.0 - 2.0 * ratio);
	const auto &a = points[index], &b = points[next];
	offset = {float(std::lerp(double(a[0]), double(b[0]), ratio)),
	          float(std::lerp(double(a[1]), double(b[1]), ratio)),
	          float(std::lerp(double(a[2]), double(b[2]), ratio))};
	return true;
}
