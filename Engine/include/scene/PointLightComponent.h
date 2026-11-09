#pragma once

#include "graphics/PointLightData.h"
#include <array>
#include <cmath>

/// <summary>
/// 物体のワールド位置から全方向を照らす光源。範囲は親や自身の拡縮によらないワールド距離
/// </summary>
struct PointLightComponent {
	static constexpr float kMaxIntensity = PointLightData::kMaxIntensity;
	static constexpr float kMaxRange = PointLightData::kMaxRange;
	std::array<float, 3> color{1.0f, 1.0f, 1.0f};
	float intensity = 4.0f;
	float range = 8.0f;

	/// <summary>
	/// 色は0から1、強さは0から上限、範囲は正の有限なワールド距離か確認する
	/// </summary>
	/// <returns>true:有効 false:非有限または範囲外</returns>
	bool IsValid() const {
		for (const auto value : color) {
			if (!std::isfinite(value) || value < 0 || value > 1) {
				return false;
			}
		}

		return std::isfinite(intensity) && intensity >= 0 &&
		       intensity <= kMaxIntensity && std::isfinite(range) &&
		       range > 0 && range <= kMaxRange;
	}

	/// <summary>
	/// 光源の全設定が一致するか確認する
	/// </summary>
	/// <param name="other">比較する設定</param>
	/// <returns>true:一致 false:変更あり</returns>
	bool operator==(const PointLightComponent &other) const = default;
};
