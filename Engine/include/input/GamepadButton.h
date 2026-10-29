#pragma once

#include <cstddef>

enum class GamepadButton : std::size_t {
	// 右側の4ボタンを位置で表す
	SOUTH, // Xbox: A / PlayStation: ×
	EAST,  // Xbox: B / PlayStation: ○
	WEST,  // Xbox: X / PlayStation: □
	NORTH, // Xbox: Y / PlayStation: △
	DPAD_UP,
	DPAD_DOWN,
	DPAD_LEFT,
	DPAD_RIGHT,
	LEFT_SHOULDER,
	RIGHT_SHOULDER,
	LEFT_STICK,
	RIGHT_STICK,
	MENU,
	VIEW,
	COUNT
};
