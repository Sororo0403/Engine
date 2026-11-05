#pragma once

#include "scene/SceneObjectId.h"
#include <compare>

/// <summary>
/// 少なくとも一方がTriggerであるColliderの組。IDは小さい順に保持する
/// </summary>
struct TriggerContact {
	SceneObjectId first = 0;
	SceneObjectId second = 0;

	/// <summary>
	/// 物体IDの組を比較し、同じ接触を重複せず管理する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>first、secondの順による比較結果</returns>
	auto operator<=>(const TriggerContact &other) const = default;
};
