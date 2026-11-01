#include "diagnostics/FrameTimingHistory.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

bool FrameTimingHistory::AddSample(float seconds) {
	if (!std::isfinite(seconds) || seconds <= 0.0f) {
		return false;
	}

	samples_[nextIndex_] = seconds;
	nextIndex_ = (nextIndex_ + 1) % kCapacity;
	count_ = (std::min)(count_ + 1, kCapacity);
	return true;
}

void FrameTimingHistory::Clear() {
	nextIndex_ = 0;
	count_ = 0;
}

FrameTimingHistory::Summary FrameTimingHistory::GetSummary() const {
	Summary result;
	result.sampleCount = count_;

	if (count_ == 0) {
		return result;
	}

	std::array<float, kCapacity> sorted;

	for (std::size_t i = 0; i < count_; ++i) {
		const float seconds = GetSampleSeconds(i);
		sorted[i] = seconds;
		result.durationSeconds += seconds;
		result.longIntervalCount += seconds >= kLongIntervalSeconds;
	}

	std::sort(sorted.begin(), sorted.begin() + count_);
	result.framesPerSecond =
	    static_cast<double>(count_) / result.durationSeconds;
	result.meanMilliseconds =
	    result.durationSeconds * 1000.0 / static_cast<double>(count_);
	result.minimumMilliseconds = static_cast<double>(sorted[0]) * 1000.0;
	result.maximumMilliseconds =
	    static_cast<double>(sorted[count_ - 1]) * 1000.0;
	const std::size_t percentileIndex = (count_ * 95 + 99) / 100 - 1;
	result.percentile95Milliseconds =
	    static_cast<double>(sorted[percentileIndex]) * 1000.0;
	return result;
}

float FrameTimingHistory::GetSampleSeconds(std::size_t index) const {
	if (index >= count_) {
		throw std::out_of_range("FrameTimingHistory: 記録の位置が範囲外です。");
	}

	const auto oldest = (nextIndex_ + kCapacity - count_) % kCapacity;
	return samples_[(oldest + index) % kCapacity];
}
