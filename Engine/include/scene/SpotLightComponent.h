#pragma once

#include "graphics/SpotLightData.h"
#include <array>
#include <cmath>

/// <summary>
/// 物体のローカル+Zを向く光源。範囲と円錐の全角は親や自身の拡縮によらない
/// </summary>
struct SpotLightComponent {
	static constexpr float kMaxIntensity = SpotLightData::kMaxIntensity;
	static constexpr float kMaxRange = SpotLightData::kMaxRange;
	static constexpr float kMinOuterAngle = .1f;
	static constexpr float kMaxOuterAngle = 179.0f;
	std::array<float, 3> color{1, 1, 1};
	float intensity = 4.0f;
	float range = 8.0f;
	float innerAngle = 30.0f;
	float outerAngle = 60.0f;

	/// <summary>
	/// 色・強さ・正の範囲と、度数法で指定する0以上の内角・それ以上の外角を確認する
	/// </summary>
	/// <returns>true:有効 false:非有限・範囲外・逆転した内外角</returns>
	bool IsValid() const {
		for (const auto value : color) {
			if (!std::isfinite(value) || value < 0 || value > 1) {
				return false;
			}
		}

		return std::isfinite(intensity) && intensity >= 0 &&
		       intensity <= kMaxIntensity && std::isfinite(range) &&
		       range > 0 && range <= kMaxRange && std::isfinite(innerAngle) &&
		       std::isfinite(outerAngle) && innerAngle >= 0 &&
		       outerAngle >= kMinOuterAngle && outerAngle <= kMaxOuterAngle &&
		       innerAngle <= outerAngle;
	}

	/// <summary>
	/// 光源の全設定が一致するか確認する
	/// </summary>
	/// <param name="other">比較する設定</param>
	/// <returns>true:一致 false:変更あり</returns>
	bool operator==(const SpotLightComponent &other) const = default;
};
