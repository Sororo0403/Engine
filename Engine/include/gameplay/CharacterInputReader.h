#pragma once

#include "gameplay/CharacterInput.h"
#include "gameplay/GameInputBindings.h"
#include "graphics/camera/CameraPose.h"
#include "input/Keyboard.h"
#include "math/MathUtils.h"
#include <cmath>

namespace CharacterInputReader {
inline constexpr double kMinimumHeadingRatio = 0.000001;

/// <summary>
/// 共通キーボードから割り当て済みの移動とジャンプ押下を取得する
/// </summary>
/// <param name="keyboard">呼び出し側でフォーカスを確認したキーボード</param>
/// <param name="bindings">有効なキー設定。未指定はWASD移動とSpaceジャンプ</param>
/// <param name="movementCamera">指定時は水平な注視方向に移動を合わせる。真上・真下に近い向きはワールド基準。nullptrは常にワールド基準</param>
/// <returns>ワールドX/Z方向の入力とジャンプ押下。キー設定やカメラ位置・注視点が無効な場合は全てゼロ</returns>
inline CharacterInput Read(const Keyboard &keyboard,
                           const GameInputBindings &bindings = {},
                           const CameraPose *movementCamera = nullptr) {
	if (!bindings.IsValid()) {
		return {};
	}

	CharacterInput input{float(keyboard.IsDown(bindings.right)) -
	                         float(keyboard.IsDown(bindings.left)),
	                     float(keyboard.IsDown(bindings.forward)) -
	                         float(keyboard.IsDown(bindings.backward)),
	                     keyboard.IsPressed(bindings.jump)};

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
	const double horizontal = std::hypot(x, z);

	if (length == 0.0) {
		return {};
	}

	if (horizontal <= length * kMinimumHeadingRatio) {
		return input;
	}

	const double forwardX = x / horizontal;
	const double forwardZ = z / horizontal;
	return {
	    static_cast<float>(input.moveX * forwardZ + input.moveZ * forwardX),
	    static_cast<float>(-input.moveX * forwardX + input.moveZ * forwardZ),
	    input.jumpPressed};
}
} // namespace CharacterInputReader
