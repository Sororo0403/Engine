#pragma once

#include "input/MouseButton.h"

#include <array>
#include <cstdint>

/// <summary>
/// マウスの入力状態
/// </summary>
struct MouseState {
	bool connected = false;

	std::array<bool, static_cast<std::size_t>(MouseButton::COUNT)> buttons{};

	// 前回の取得からの相対移動量。画面上のピクセル数ではない
	std::int64_t deltaX = 0;
	std::int64_t deltaY = 0;

	// 前回の取得からのホイール変化量。GameInputの値を保持する
	std::int64_t wheelX = 0;
	std::int64_t wheelY = 0;

	// 入力対象ウィンドウのクライアント座標
	bool positionValid = false;
	int positionX = 0;
	int positionY = 0;
};
