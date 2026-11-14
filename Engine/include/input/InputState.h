#pragma once

#include "input/GamepadState.h"
#include "input/Keyboard.h"
#include "input/MouseState.h"
#include "input/WiiRemoteState.h"

/// <summary>
/// バックエンドが取得した入力状態
/// </summary>
struct InputState {
	bool active = false;

	Keyboard::State keyboard{};
	MouseState mouse{};
	GamepadState gamepad{};
	WiiRemoteState wiiRemote{};
};
