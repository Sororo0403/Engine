#pragma once
#include "scene/SceneObjectId.h"
#include <compare>

/// <summary>
/// TriggerではないColliderの接触。物体IDを小さい順に保持する
/// </summary>
struct CollisionContact {
	SceneObjectId first = 0;
	SceneObjectId second = 0;

	/// <summary>
	/// 同じ組を重複せず、ID順で管理する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>first、secondの順の比較結果</returns>
	auto operator<=>(const CollisionContact &other) const = default;
};
