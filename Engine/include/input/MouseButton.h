#pragma once

#include <cstddef>

/// <summary>
/// 共通のマウスボタン
/// </summary>
enum class MouseButton : std::size_t {
	LEFT,
	RIGHT,
	MIDDLE,
	BUTTON4,
	BUTTON5,
	COUNT
};
