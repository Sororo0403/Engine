#pragma once

#include "gameplay/CharacterInputReader.h"
#include "gameplay/OrbitCameraInputReader.h"
#include "graphics/ui/ViewportInput.h"
#include "input/Keyboard.h"

namespace GameplayInput {
/// <summary>
/// Gameが入力可能な場合だけ、共通キーボードと任意のゲームパッドから移動とジャンプを作成する
/// </summary>
/// <param name="keys">ゲーム用の共通キーボード</param>
/// <param name="viewport">Gameの画像とフォーカス状態</param>
/// <param name="textEditing">文字入力中か</param>
/// <param name="bindings">Game Settingsで指定した操作キー</param>
/// <param name="movementCamera">指定時はこのカメラの水平な注視方向を移動基準にする</param>
/// <param name="gamepad">共通ゲームパッド。nullptrはキーボードだけを使う</param>
/// <returns>入力不可の場合は全てゼロの操作</returns>
inline CharacterInput Read(const Keyboard &keys, const ViewportInput &viewport,
                           bool textEditing,
                           const GameInputBindings &bindings = {},
                           const CameraPose *movementCamera = nullptr,
                           const Gamepad *gamepad = nullptr) {
	if (!viewport.windowFocused || !viewport.applicationFocused ||
	    viewport.widgetActive || textEditing) {
		return {};
	}

	return CharacterInputReader::Read(keys, bindings, movementCamera, gamepad);
}

/// <summary>
/// Gameが入力可能な場合だけ旋回を読み、マウスは画像の上にある場合だけ受け付ける
/// </summary>
/// <param name="mouse">共通マウス</param>
/// <param name="viewport">Game画像とフォーカス状態</param>
/// <param name="textEditing">文字入力中か</param>
/// <param name="gamepad">ゲームパッドを許可する場合に指定する</param>
/// <returns>操作可能な入力。それ以外は全てゼロ</returns>
inline OrbitCameraInput ReadOrbit(const Mouse &mouse,
                                  const ViewportInput &viewport,
                                  bool textEditing,
                                  const Gamepad *gamepad = nullptr) {
	if (!viewport.windowFocused || !viewport.applicationFocused ||
	    viewport.widgetActive || textEditing) {
		return {};
	}

	return OrbitCameraInputReader::Read(
	    viewport.imageHovered ? &mouse : nullptr, gamepad);
}
} // namespace GameplayInput
