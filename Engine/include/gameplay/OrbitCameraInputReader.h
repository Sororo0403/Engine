#pragma once

#include "gameplay/OrbitCameraInput.h"
#include "gameplay/GameInputBindings.h"
#include "input/Gamepad.h"
#include "input/Mouse.h"

namespace OrbitCameraInputReader {
/// <summary>
/// 共通入力から右ドラッグ・ホイール・右スティックの操作を読み取る
/// </summary>
/// <param name="mouse">マウスを許可しない場合はnullptr。ホイールはWindowsの120単位を1目盛りへ変換する</param>
/// <param name="gamepad">ゲームパッドを許可しない場合はnullptr</param>
/// <returns>右ドラッグ中の相対回転、ホイールのズーム、右スティックの回転。中ボタンまたは右スティック押下で初期姿勢へ戻す</returns>
inline OrbitCameraInput Read(const Mouse *mouse,
                             const Gamepad *gamepad = nullptr,
                             const GameInputBindings::Camera &bindings = {}) {
	OrbitCameraInput result;

	if (mouse && mouse->IsConnected()) {
		const auto &state = mouse->GetState();

		if (bindings.rotate && mouse->IsDown(*bindings.rotate)) {
			result.mouseX = static_cast<float>(state.deltaX);
			result.mouseY = static_cast<float>(state.deltaY);
		}

		if (bindings.wheelZoom)
			result.zoom = static_cast<float>(state.wheelY) / 120.0f;
		result.reset = bindings.mouseReset && mouse->IsPressed(*bindings.mouseReset);
	}

	if (gamepad && gamepad->IsConnected()) {
		if (bindings.stick != GameInputBindings::MoveStick::NONE) {
			const auto &state = gamepad->GetState();
			const auto &stick = bindings.stick == GameInputBindings::MoveStick::LEFT
			    ? state.leftStick : state.rightStick;
			result.stickX = stick.x;
			result.stickY = stick.y;
		}
		result.reset |= bindings.padReset && gamepad->IsPressed(*bindings.padReset);
	}

	return result;
}
} // namespace OrbitCameraInputReader
