#include "audio/WaveData.h"

#include <bit>
#include <cmath>

std::size_t WaveData::GetBytesPerSample() const {
	switch (format) {
	case WaveSampleFormat::PCM8:
		return 1;
	case WaveSampleFormat::PCM16:
		return 2;
	case WaveSampleFormat::PCM24:
		return 3;
	case WaveSampleFormat::PCM32:
	case WaveSampleFormat::FLOAT32:
		return 4;
	}

	return 0;
}

std::uint64_t WaveData::GetFrameCount() const {
	const auto bytes = GetBytesPerSample();
	return bytes && (channels == 1 || channels == 2)
	           ? samples.size() / (bytes * channels)
	           : 0;
}

double WaveData::GetDurationSeconds() const {
	return sampleRate ? double(GetFrameCount()) / sampleRate : 0;
}

bool WaveData::IsValid() const {
	const auto bytes = GetBytesPerSample();
	if (!bytes || (channels != 1 && channels != 2) || sampleRate < 8000 ||
	    sampleRate > 192000 || samples.empty() ||
	    samples.size() > kMaximumBytes ||
	    samples.size() % (bytes * channels) != 0) {
		return false;
	}

	if (format == WaveSampleFormat::FLOAT32) {
		for (std::size_t i = 0; i < samples.size(); i += 4) {
			const std::uint32_t bits = std::uint32_t(samples[i]) |
			                           (std::uint32_t(samples[i + 1]) << 8) |
			                           (std::uint32_t(samples[i + 2]) << 16) |
			                           (std::uint32_t(samples[i + 3]) << 24);
			if (!std::isfinite(std::bit_cast<float>(bits))) {
				return false;
			}
		}
	}

	return true;
}
