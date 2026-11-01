#pragma once

#include <DirectXMath.h>

#include <limits>

/// <summary>
/// 始点・方向・最大距離で表すレイ
/// </summary>
struct Ray {
	DirectX::XMFLOAT3 origin{0.0f, 0.0f, 0.0f};

	// 長さ1の方向ベクトル
	DirectX::XMFLOAT3 direction{0.0f, 0.0f, 1.0f};

	// 始点から判定する最大距離
	float maxDistance = (std::numeric_limits<float>::max)();
};
