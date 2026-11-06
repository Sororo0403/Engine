#pragma once

#include "scene/SceneObjectId.h"

/// <summary>
/// 成功したシミュレーションから音声へ渡す、1音源の再生または停止の要求
/// </summary>
struct SceneAudioAction {
	enum class Operation { PLAY, STOP };
	SceneObjectId target = 0;
	Operation operation = Operation::PLAY;

	/// <summary>
	/// 対象IDと操作種別が有効か確認する。対象の存在は受け手で検証する
	/// </summary>
	/// <returns>true:有効 false:未設定のIDまたは未対応の操作</returns>
	bool IsValid() const {
		return target != 0 &&
		       (operation == Operation::PLAY || operation == Operation::STOP);
	}

	/// <summary>
	/// 対象と操作内容が一致するか確認する
	/// </summary>
	/// <param name="other">比較する要求</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const SceneAudioAction &other) const = default;
};
