#pragma once

#include "physics/TriggerEvent.h"

/// <summary>
/// Triggerの進入・退出に応じて、対象物体自身の有効設定を変更する
/// </summary>
struct TriggerAction {
	/// <summary>
	/// 対象へ適用する有効設定の操作
	/// </summary>
	enum class Operation { ENABLE, DISABLE, TOGGLE };

	TriggerEvent::Type event = TriggerEvent::Type::ENTER;
	SceneObjectId target = 0;
	Operation operation = Operation::ENABLE;
	bool once = false;

	/// <summary>
	/// 種別が有効か確認する。未割り当ての対象は編集用に許可し、Play時に存在を確認する
	/// </summary>
	/// <returns>true:有効 false:未対応のイベントまたは操作</returns>
	bool IsValid() const {
		return (event == TriggerEvent::Type::ENTER ||
		        event == TriggerEvent::Type::EXIT) &&
		       (operation == Operation::ENABLE ||
		        operation == Operation::DISABLE ||
		        operation == Operation::TOGGLE);
	}

	/// <summary>
	/// 対象と実行条件が一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const TriggerAction &other) const = default;
};
