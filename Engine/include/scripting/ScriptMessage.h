#pragma once
#include "scene/SceneObjectId.h"
#include <cstddef>
#include <string>

/// <summary>
/// 次の正の時間の更新でLuaへ渡す通知。物体のアドレスを保持せず、データをJSONとして複製する
/// </summary>
struct ScriptMessage {
	static constexpr std::size_t kMaximumNameBytes = 64;
	static constexpr std::size_t kMaximumDataBytes = 4096;
	SceneObjectId receiver = 0, sender = 0;
	std::string name, data = "{}";
};
