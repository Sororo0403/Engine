#pragma once

#include "physics/TriggerEvent.h"

/// <summary>
/// Triggerの進入・退出に応じて、対象物体の有効設定または音声の再生を操作する
/// </summary>
struct TriggerAction {
	/// <summary>
	/// 対象へ適用する有効設定、またはAudio Sourceの再生・停止
	/// </summary>
	enum class Operation { ENABLE, DISABLE, TOGGLE, PLAY_AUDIO, STOP_AUDIO };

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
		        operation == Operation::TOGGLE || IsAudioOperation());
	}

	/// <summary>
	/// 対象にAudio Sourceが必要な操作か確認する
	/// </summary>
	/// <returns>true:音声操作 false:有効設定の操作または未対応の値</returns>
	bool IsAudioOperation() const {
		return operation == Operation::PLAY_AUDIO ||
		       operation == Operation::STOP_AUDIO;
	}

	/// <summary>
	/// 対象と実行条件が一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const TriggerAction &other) const = default;
};
