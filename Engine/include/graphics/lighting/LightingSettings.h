#pragma once

#include "graphics/lighting/ShadowSettings.h"
#include <DirectXMath.h>
#include <cmath>

/// <summary>
/// ゲーム全体に共通する平行光源と環境光の設定
/// </summary>
struct LightingSettings {
	static constexpr float kMaxIntensity = 16.0f;
	DirectX::XMFLOAT3 direction{1.0f, -1.0f, 1.0f};
	float intensity = 0.8f;
	DirectX::XMFLOAT3 color{1.0f, 1.0f, 1.0f};
	float ambient = 0.2f;
	ShadowSettings shadows;

	/// <summary>
	/// 光の向きが有限かつ非零で、色・環境光が0から1、強さが許容範囲内か確認する
	/// </summary>
	/// <returns>true:有効 false:範囲外または非有限</returns>
	bool IsValid() const {
		const double lengthSquared = double(direction.x) * direction.x +
		                             double(direction.y) * direction.y +
		                             double(direction.z) * direction.z;
		auto unit = [](float value) {
			return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
		};
		return shadows.IsValid() && std::isfinite(lengthSquared) &&
		       lengthSquared > 0.0 && std::isfinite(intensity) &&
		       intensity >= 0.0f && intensity <= kMaxIntensity &&
		       unit(color.x) && unit(color.y) && unit(color.z) && unit(ambient);
	}

	/// <summary>
	/// 編集値が全て一致するか確認する
	/// </summary>
	/// <param name="other">比較する照明</param>
	/// <returns>true:一致 false:変更あり</returns>
	bool operator==(const LightingSettings &other) const {
		return direction.x == other.direction.x &&
		       direction.y == other.direction.y &&
		       direction.z == other.direction.z &&
		       intensity == other.intensity && color.x == other.color.x &&
		       color.y == other.color.y && color.z == other.color.z &&
		       ambient == other.ambient && shadows == other.shadows;
	}
};
