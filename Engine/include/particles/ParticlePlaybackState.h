#pragma once
#include <cstddef>

/// <summary>
/// 発生元の時計・発生指定と、生存する粒子数
/// </summary>
struct ParticlePlaybackState {
	double seconds = 0;
	std::size_t count = 0;
	bool emitting = false;
};
