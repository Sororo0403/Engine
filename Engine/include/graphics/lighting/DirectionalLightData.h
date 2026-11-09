#pragma once

#include <DirectXMath.h>
#include <array>
#include <cstdint>

/// <summary>
/// GPUへ渡す平行光源の情報
/// </summary>
struct DirectionalLightData {
	DirectX::XMFLOAT3 direction{1.0f, -1.0f, 1.0f};
	float intensity = 0.8f;

	DirectX::XMFLOAT3 color{1.0f, 1.0f, 1.0f};
	float padding = 0.0f;
};

/// <summary>
/// 環境光と複数の平行光をGPUへ渡す定数バッファ
/// </summary>
struct DirectionalLightsData {
	static constexpr std::uint32_t kMaximumLights = 32;
	float ambient = 0.2f;
	std::uint32_t count = 1;
	std::int32_t shadowIndex = -1;
	float padding = 0.0f;
	std::array<DirectionalLightData, kMaximumLights> lights{};
};
