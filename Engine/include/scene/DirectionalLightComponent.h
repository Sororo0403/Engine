#pragma once

#include "graphics/lighting/LightingSettings.h"
#include <algorithm>
#include <array>
#include <cmath>

/// <summary>
/// ワールド回転の+Z方向へ照射する平行光。位置と拡縮は照明に影響しない
/// </summary>
struct DirectionalLightComponent {
	std::array<float, 3> color{1, 1, 1};
	float intensity = 0.8f;
	ShadowSettings shadows;

	/// <summary>
	/// 色、光の強さ、影の設定が有効か確認する
	/// </summary>
	bool IsValid() const {
		return std::ranges::all_of(color, [](float value) {
			       return std::isfinite(value) && value >= 0 && value <= 1;
		       }) &&
		       std::isfinite(intensity) && intensity >= 0 &&
		       intensity <= LightingSettings::kMaxIntensity && shadows.IsValid();
	}

	bool operator==(const DirectionalLightComponent &) const = default;
};
