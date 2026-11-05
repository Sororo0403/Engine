#pragma once

#include <DirectXMath.h>
#include <cmath>
#include <cstddef>

/// <summary>
/// 描画へ渡す点光源。シェーダーと同じ32バイトの配置で保持する
/// </summary>
struct PointLightData {
	static constexpr std::size_t kMaximumLights = 32;
	static constexpr float kMaxRange = 100000.0f;
	static constexpr float kMaxIntensity = 16.0f;
	DirectX::XMFLOAT3 position{};
	float range = 8.0f;
	DirectX::XMFLOAT3 color{1, 1, 1};
	float intensity = 4.0f;

	/// <summary>
	/// GPUで使用する位置・範囲・色・強さが有限か確認する
	/// </summary>
	/// <returns>true:有効 false:非有限または範囲外</returns>
	bool IsValid() const {
		auto unit = [](float value) {
			return std::isfinite(value) && value >= 0 && value <= 1;
		};
		return std::isfinite(position.x) && std::isfinite(position.y) &&
		       std::isfinite(position.z) && std::isfinite(range) && range > 0 &&
		       range <= kMaxRange && std::isfinite(intensity) &&
		       intensity >= 0 && intensity <= kMaxIntensity && unit(color.x) &&
		       unit(color.y) && unit(color.z);
	}
};
