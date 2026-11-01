#pragma once
#include "gameplay/CharacterInput.h"
#include "input/Keyboard.h"

namespace CharacterInputReader {
/// <summary>
/// 共通キーボードからWASD移動とSpace押下を取得する
/// </summary>
/// <param name="keyboard">呼び出し側でフォーカスを確認したキーボード</param>
/// <returns>ワールドX/Z方向の入力とジャンプの押下状態</returns>
inline CharacterInput Read(const Keyboard &keyboard) {
	return {float(keyboard.IsDown(Key::D)) - float(keyboard.IsDown(Key::A)),
	        float(keyboard.IsDown(Key::W)) - float(keyboard.IsDown(Key::S)),
	        keyboard.IsPressed(Key::SPACE)};
}
} // namespace CharacterInputReader
