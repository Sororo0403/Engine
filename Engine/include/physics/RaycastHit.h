#pragma once
#include "scene/SceneObjectId.h"
#include <DirectXMath.h>

/// <summary>
/// レイとColliderの最も近い交点。objectIdが0なら交点なし
/// </summary>
struct RaycastHit {
	SceneObjectId objectId = 0;
	float distance = 0;
	DirectX::XMFLOAT3 position{};
	DirectX::XMFLOAT3 normal{};
};
