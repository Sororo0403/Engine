#pragma once
#include "physics/CollisionContact.h"

/// <summary>
/// 通常のColliderへの接触開始・継続・終了。Triggerイベントとは別に扱う
/// </summary>
struct CollisionEvent {
	enum class Type { ENTER, STAY, EXIT };
	CollisionContact contact;
	Type type = Type::ENTER;

	/// <summary>
	/// 接触の組と種類が一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const CollisionEvent &other) const = default;
};
