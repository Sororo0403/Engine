#pragma once

#include "gameplay/CharacterInputReader.h"
#include "graphics/ui/ViewportInput.h"
#include "input/Keyboard.h"

namespace GameplayInput {
/// <summary>
/// Gameが入力可能な場合だけ、共通キーボードから移動とジャンプを作成する
/// </summary>
/// <param name="keys">ゲーム用の共通キーボード</param>
/// <param name="viewport">Gameの画像とフォーカス状態</param>
/// <param name="textEditing">文字入力中か</param>
/// <param name="bindings">Game Settingsで指定した操作キー</param>
/// <returns>入力不可の場合は全てゼロの操作</returns>
inline CharacterInput Read(const Keyboard &keys, const ViewportInput &viewport,
                           bool textEditing,
                           const GameInputBindings &bindings = {}) {
	if (!viewport.windowFocused || !viewport.applicationFocused ||
	    viewport.widgetActive || textEditing) {
		return {};
	}

	return CharacterInputReader::Read(keys, bindings);
}
} // namespace GameplayInput
