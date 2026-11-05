#pragma once

#include "gameplay/TriggerAction.h"
#include <algorithm>
#include <cstddef>
#include <vector>

/// <summary>
/// 同じ物体のTriggerイベントで実行する操作を、設定順に保持する
/// </summary>
struct TriggerActionsComponent {
	static constexpr std::size_t kMaximumActions = 64;
	std::vector<TriggerAction> actions;

	/// <summary>
	/// 操作数と各設定の種別を確認する。空は動作なしとして許可する
	/// </summary>
	/// <returns>true:有効 false:上限超過または種別が無効</returns>
	bool IsValid() const {
		return actions.size() <= kMaximumActions &&
		       std::all_of(actions.begin(), actions.end(),
		                   [](const TriggerAction &action) {
			                   return action.IsValid();
		                   });
	}

	/// <summary>
	/// 設定順を含めた全操作が一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const TriggerActionsComponent &other) const = default;
};
