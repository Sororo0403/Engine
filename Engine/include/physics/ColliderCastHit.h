#pragma once
#include <DirectXMath.h>

/// <summary>
/// 直線移動で最初に接触する位置の割合と、障害物から移動形状へ向く単位法線
/// </summary>
struct ColliderCastHit {
	double fraction = 1;
	DirectX::XMFLOAT3 normal{};
};
