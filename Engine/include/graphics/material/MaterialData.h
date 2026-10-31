#pragma once

#include <DirectXMath.h>

/// <summary>
/// GPUへ渡す材質の情報
/// </summary>
struct MaterialData {
	// テクスチャの色に掛けるRGBA
	DirectX::XMFLOAT4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};

	// UV座標に掛ける倍率
	DirectX::XMFLOAT2 uvScale{1.0f, 1.0f};

	// UV座標に加える移動量
	DirectX::XMFLOAT2 uvOffset{0.0f, 0.0f};
};
