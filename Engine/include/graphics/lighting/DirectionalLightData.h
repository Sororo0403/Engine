#pragma once

#include <DirectXMath.h>

/// <summary>
/// GPUへ渡す平行光源の情報
/// </summary>
struct DirectionalLightData {
	DirectX::XMFLOAT3 direction{1.0f, -1.0f, 1.0f};
	float intensity = 0.8f;

	DirectX::XMFLOAT3 color{1.0f, 1.0f, 1.0f};
	float ambient = 0.2f;
};
