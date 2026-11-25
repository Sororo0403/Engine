#pragma once
#include "ui/UIInput.h"
#include "gameplay/GameInputBindings.h"
class Keyboard;
class Mouse;
class Gamepad;

namespace UIInputReader {
/// <summary>
/// キーボード・パッドと、任意のクライアント座標のマウスをゲーム内UIの入力へ変換する
/// </summary>
/// <param name="keyboard">最新のキー状態</param>
/// <param name="mouse">Runtimeのマウス。Editorの画像内ポインターを別途反映する場合はnullptr</param>
/// <param name="gamepad">使用するパッド。無効時はnullptr</param>
/// <param name="width">実際に描画する領域の幅</param>
/// <param name="height">実際に描画する領域の高さ</param>
/// <param name="enabled">アプリにフォーカスがあり操作を受け付けるか</param>
/// <param name="navigationEnabled">キーボード・パッドの選択も受け付けるか</param>
/// <returns>正規化された入力。描画寸法が無効な場合は操作を無効にする</returns>
UIInput Read(const Keyboard &keyboard, const Mouse *mouse,
             const Gamepad *gamepad, int width, int height, bool enabled = true,
             bool navigationEnabled = true, const GameInputBindings::UI &bindings = {});
} // namespace UIInputReader
