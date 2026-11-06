#pragma once

#include "scene/SceneAudioAction.h"
#include <cstdint>
#include <span>

/// <summary>
/// 1回のシミュレーション成功で確定した音声要求を参照する。次の成功・停止まで有効
/// </summary>
struct SceneAudioActions {
	// 一時停止や0秒更新では変わらず、受け手が同じ操作を重複して実行しないために使う。
	std::uint64_t revision = 0;
	std::span<const SceneAudioAction> commands;
};
