#include "core/FrameTimer.h"

void FrameTimer::Reset() {
	previousTime_ = Clock::now();
}

float FrameTimer::Tick() {
	const auto now = Clock::now();

	const std::chrono::duration<float> elapsed = now - previousTime_;

	previousTime_ = now;

	return elapsed.count();
}
