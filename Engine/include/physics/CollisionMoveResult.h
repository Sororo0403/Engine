#pragma once

#include <DirectXMath.h>

/// <summary>
/// 衝突を反映した移動量と接触状態
/// </summary>
struct CollisionMoveResult {
	DirectX::XMFLOAT3 displacement{};
	bool grounded = false;
	bool hitCeiling = false;
	bool hitWall = false;
};
