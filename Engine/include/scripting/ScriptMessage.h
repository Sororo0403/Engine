#pragma once
#include "scene/SceneObjectId.h"
#include <cstddef>
#include <string>
#include <string_view>

/// <summary>
/// 次の正の時間の更新でLuaへ渡す通知。物体のアドレスを保持せず、データをJSONとして複製する
/// </summary>
struct ScriptMessage {
	static constexpr std::size_t kMaximumNameBytes = 64;
	static constexpr std::size_t kMaximumDataBytes = 4096;
	SceneObjectId receiver = 0, sender = 0;
	std::string name, data = "{}";

	/// <summary>
	/// 通知名が長さ制限を満たすUTF-8か確認する
	/// </summary>
	/// <param name="name">検証する通知名</param>
	/// <returns>true:有効 false:空・長すぎる・不正な文字列</returns>
	static bool IsValidName(std::string_view name);
};
