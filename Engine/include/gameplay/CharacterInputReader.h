#pragma once

#include "gameplay/CharacterInput.h"
#include "gameplay/CharacterSettings.h"
#include "gameplay/GameInputBindings.h"
#include "graphics/camera/CameraPose.h"
#include "input/Gamepad.h"
#include "input/Keyboard.h"
#include "math/MathUtils.h"
#include <algorithm>
#include <cmath>

namespace CharacterInputReader {
inline constexpr double kMinimumHeadingRatio = 0.000001;

/// <summary>
/// 共通キーボードと任意のゲームパッドから、移動とジャンプ・操作の押下を合成する
/// </summary>
/// <param name="keyboard">呼び出し側でフォーカスを確認したキーボード</param>
/// <param name="bindings">有効なキー設定。未指定はWASD移動とSpaceジャンプ</param>
/// <param name="movementCamera">指定時は注視方向を重力に垂直な平面へ投影し、移動を合わせる</param>
/// <param name="gamepad">指定時は設定された移動軸とボタン入力を加える。nullptrや未接続はキーボードだけを使う</param>
/// <param name="character">操作対象の移動平面。未指定はワールドXZ平面</param>
/// <returns>移動平面上の入力とジャンプ押下。キー設定やカメラ位置・注視点が無効な場合は全てゼロ</returns>
inline CharacterInput Read(const Keyboard &keyboard,
                           const GameInputBindings &bindings = {},
                           const CameraPose *movementCamera = nullptr,
                           const Gamepad *gamepad = nullptr,
                           const CharacterSettings *character = nullptr) {
	if (!bindings.IsValid()) {
		return {};
	}

	auto moveDown = [&](Key primary, Key arrow) {
		if (keyboard.IsDown(primary))
			return true;
		return bindings.arrowMovement && keyboard.IsDown(arrow) &&
		       std::ranges::none_of(GameInputBindings::kActions,
		                            [&](const auto &action) {
			                            return bindings.*action.member == arrow;
		                            });
	};

	CharacterInput input{float(moveDown(bindings.right, Key::RIGHT)) -
	                         float(moveDown(bindings.left, Key::LEFT)),
	                     float(moveDown(bindings.forward, Key::UP)) -
	                         float(moveDown(bindings.backward, Key::DOWN)),
	                     keyboard.IsPressed(bindings.jump),
	                     keyboard.IsPressed(bindings.interact)};

	if (gamepad && gamepad->IsConnected()) {
		if (bindings.moveStick != GameInputBindings::MoveStick::NONE) {
			const auto &state = gamepad->GetState();
			const auto &stick = bindings.moveStick == GameInputBindings::MoveStick::LEFT
			    ? state.leftStick : state.rightStick;
			input.moveX += stick.x;
			input.moveZ += stick.y;
		}
		if (bindings.dpadMovement) {
			input.moveX += float(gamepad->IsDown(GamepadButton::DPAD_RIGHT)) -
			               float(gamepad->IsDown(GamepadButton::DPAD_LEFT));
			input.moveZ += float(gamepad->IsDown(GamepadButton::DPAD_UP)) -
			               float(gamepad->IsDown(GamepadButton::DPAD_DOWN));
		}
		input.jumpPressed |= bindings.gamepadJump && gamepad->IsPressed(*bindings.gamepadJump);
		input.interactPressed |= bindings.gamepadInteract && gamepad->IsPressed(*bindings.gamepadInteract);
	}

	if (!movementCamera) {
		return input;
	}

	if (!MathUtils::IsFinite(movementCamera->position) ||
	    !MathUtils::IsFinite(movementCamera->target)) {
		return {};
	}

	const double x = static_cast<double>(movementCamera->target.x) -
	                 movementCamera->position.x;
	const double y = static_cast<double>(movementCamera->target.y) -
	                 movementCamera->position.y;
	const double z = static_cast<double>(movementCamera->target.z) -
	                 movementCamera->position.z;
	const double length = std::hypot(x, y, z);
	const auto [right, forward] = character ? character->GetMovementAxes()
	                                        : CharacterSettings{}.GetMovementAxes();
	const double projectedX = x * right.x + y * right.y + z * right.z;
	const double projectedZ = x * forward.x + y * forward.y + z * forward.z;
	const double horizontal = std::hypot(projectedX, projectedZ);

	if (length == 0.0) {
		return {};
	}

	if (horizontal <= length * kMinimumHeadingRatio) {
		return input;
	}

	const double forwardX = projectedX / horizontal;
	const double forwardZ = projectedZ / horizontal;
	return {
	    static_cast<float>(input.moveX * forwardZ + input.moveZ * forwardX),
	    static_cast<float>(-input.moveX * forwardX + input.moveZ * forwardZ),
	    input.jumpPressed, input.interactPressed};
}
} // namespace CharacterInputReader
