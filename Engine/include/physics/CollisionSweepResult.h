#pragma once

#include "scene/SceneObjectId.h"
#include <DirectXMath.h>

/// <summary>
/// ボックスの直線移動で最初に体積が重なり始める接触。滑りや離隔距離は適用しない
/// </summary>
struct CollisionSweepResult {
	// 接触したColliderの物体ID。0は接触なし
	SceneObjectId objectId = 0;

	// 要求する移動量に対する接触位置の割合。0は開始位置での重なり、1は終点または接触なし
	double fraction = 1.0;
	// 障害物から問い合わせ元へ向く単位法線。法線を返さない旧形式の問い合わせでは0
	DirectX::XMFLOAT3 normal{};
};
