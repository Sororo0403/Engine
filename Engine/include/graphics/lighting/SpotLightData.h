#pragma once

#include <DirectXMath.h>
#include <array>
#include <cmath>
#include <cstddef>

/// <summary>
/// 描画へ渡すスポットライト。シェーダーと同じ64バイトの配置で保持する
/// </summary>
struct SpotLightData {
	static constexpr std::size_t kMaximumLights = 32;
	static constexpr float kMaxRange = 100000.0f;
	static constexpr float kMaxIntensity = 16.0f;
	DirectX::XMFLOAT3 position{};
	float range = 8.0f;
	DirectX::XMFLOAT3 direction{0, 0, 1};
	float cosInner = 0.9659258263f;
	DirectX::XMFLOAT3 color{1, 1, 1};
	float intensity = 4.0f;
	float cosOuter = 0.8660254038f;
	std::array<float, 3> padding{};

	/// <summary>
	/// 位置・単位方向・範囲・色・強さと、内外の半角の余弦が有限か確認する
	/// </summary>
	/// <returns>true:有効 false:非有限・範囲外・非単位方向・逆転した円錐</returns>
	bool IsValid() const {
		const auto unit = [](float value) {
			return std::isfinite(value) && value >= 0 && value <= 1;
		};
		const double lengthSquared = double(direction.x) * direction.x +
		                             double(direction.y) * direction.y +
		                             double(direction.z) * direction.z;
		return std::isfinite(position.x) && std::isfinite(position.y) &&
		       std::isfinite(position.z) && std::isfinite(lengthSquared) &&
		       std::abs(lengthSquared - 1) <= .001 && std::isfinite(range) &&
		       range > 0 && range <= kMaxRange && std::isfinite(intensity) &&
		       intensity >= 0 && intensity <= kMaxIntensity && unit(color.x) &&
		       unit(color.y) && unit(color.z) && unit(cosInner) &&
		       unit(cosOuter) && cosOuter > 0 && cosInner >= cosOuter;
	}
};
