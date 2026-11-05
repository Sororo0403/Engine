#pragma once

#include "physics/TriggerContact.h"

/// <summary>
/// Triggerへの進入・滞在・退出。相手を削除した後もIDで退出を識別できる
/// </summary>
struct TriggerEvent {
	/// <summary>
	/// 前回からの接触状態の変化
	/// </summary>
	enum class Type { ENTER, STAY, EXIT };

	TriggerContact contact;
	Type type = Type::ENTER;

	/// <summary>
	/// 接触の組とイベント種別が一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const TriggerEvent &other) const = default;
};
