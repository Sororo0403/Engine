#pragma once

#include "input/GamepadButton.h"

#include <array>

/// <summary>
/// スティックの入力値
/// </summary>
struct GamepadStick {
	// 左が-1、右が1
	float x = 0.0f;

	// 下が-1、上が1
	float y = 0.0f;
};

/// <summary>
/// コントローラーの入力状態
/// </summary>
struct GamepadState {
	bool connected = false;

	std::array<bool, static_cast<std::size_t>(GamepadButton::COUNT)> buttons{};

	GamepadStick leftStick{};
	GamepadStick rightStick{};

	// 押していない状態が0、最大まで押した状態が1
	float leftTrigger = 0.0f;
	float rightTrigger = 0.0f;
};
