#pragma once

#include "gameplay/CharacterInput.h"
#include "gameplay/GameInputBindings.h"
#include "input/Keyboard.h"

namespace CharacterInputReader {
/// <summary>
/// 共通キーボードから割り当て済みの移動とジャンプ押下を取得する
/// </summary>
/// <param name="keyboard">呼び出し側でフォーカスを確認したキーボード</param>
/// <param name="bindings">有効なキー設定。未指定はWASD移動とSpaceジャンプ</param>
/// <returns>ワールドX/Z方向の入力とジャンプ押下。設定が無効な場合は全てゼロ</returns>
inline CharacterInput Read(const Keyboard &keyboard,
                           const GameInputBindings &bindings = {}) {
	if (!bindings.IsValid()) {
		return {};
	}

	return {float(keyboard.IsDown(bindings.right)) -
	            float(keyboard.IsDown(bindings.left)),
	        float(keyboard.IsDown(bindings.forward)) -
	            float(keyboard.IsDown(bindings.backward)),
	        keyboard.IsPressed(bindings.jump)};
}
} // namespace CharacterInputReader
