#pragma once

#include "input/Keyboard.h"

/// <summary>
/// バックエンドが取得した入力状態
/// </summary>
struct InputState {
	bool active = false;

	Keyboard::State keyboard{};
};
