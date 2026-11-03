#pragma once

#include "scene/SceneObjectId.h"

/// <summary>
/// ボックスの直線移動で最初に体積が重なり始める接触。滑りや離隔距離は適用しない
/// </summary>
struct CollisionSweepResult {
	// 接触したColliderの物体ID。0は接触なし
	SceneObjectId objectId = 0;

	// 要求する移動量に対する接触位置の割合。0は開始位置での重なり、1は終点または接触なし
	double fraction = 1.0;
};
