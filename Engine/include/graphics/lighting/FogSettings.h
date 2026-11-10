#pragma once

#include <DirectXMath.h>
#include <cmath>

/// <summary>
/// カメラからの距離に応じてメッシュに適用する霧の設定
/// </summary>
struct FogSettings {
	bool enabled = false;
	DirectX::XMFLOAT3 color{0.73f, 0.85f, 0.92f};
	float start = 28.0f;
	float end = 130.0f;
	float strength = 0.67f;

	bool IsValid() const {
		const auto unit = [](float value) { return value >= 0.0f && value <= 1.0f; };
		return unit(color.x) && unit(color.y) && unit(color.z) && unit(strength) &&
		       std::isfinite(start) && std::isfinite(end) && start >= 0.0f && end > start;
	}

	bool operator==(const FogSettings &other) const {
		return enabled == other.enabled && color.x == other.color.x &&
		       color.y == other.color.y && color.z == other.color.z &&
		       start == other.start && end == other.end && strength == other.strength;
	}
};
