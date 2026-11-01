#pragma once

#include <DirectXMath.h>

/// <summary>
/// 拡大率・回転クォータニオン・移動量
/// </summary>
struct Transform {
	DirectX::XMFLOAT3 scale{1.0f, 1.0f, 1.0f};
	// x・y・z・wの順に保持する単位クォータニオン
	DirectX::XMFLOAT4 rotation{0.0f, 0.0f, 0.0f, 1.0f};
	DirectX::XMFLOAT3 translation{0.0f, 0.0f, 0.0f};
};
