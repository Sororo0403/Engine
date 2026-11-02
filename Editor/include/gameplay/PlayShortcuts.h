#pragma once

#include "gameplay/EditorPlayAction.h"
#include "input/Keyboard.h"

namespace PlayShortcuts {
/// <summary>
/// 共通キーボードの押下エッジから再生操作を取得する。修飾キー併用時は受け付けない
/// </summary>
/// <param name="keys">現在と前フレームのキー状態</param>
/// <param name="playing">再生中か</param>
/// <param name="paused">一時停止中か</param>
/// <param name="enabled">アプリが入力可能で、入力欄・ポップアップ・ドラッグなどの操作中でないか</param>
/// <returns>押した瞬間の操作。同時押しはPlay/Stop、Restart、Pause/Resume、Stepの順で優先する</returns>
inline EditorPlayAction Read(const Keyboard &keys, bool playing, bool paused,
                             bool enabled) {
	if (!enabled || keys.IsDown(Key::CTRL) || keys.IsDown(Key::SHIFT) ||
	    keys.IsDown(Key::ALT)) {
		return EditorPlayAction::NONE;
	}

	if (keys.IsPressed(Key::F5)) {
		return playing ? EditorPlayAction::STOP : EditorPlayAction::PLAY;
	}

	if (!playing) {
		return EditorPlayAction::NONE;
	}

	if (keys.IsPressed(Key::F8)) {
		return EditorPlayAction::RESTART;
	}

	if (keys.IsPressed(Key::F6)) {
		return EditorPlayAction::TOGGLE_PAUSE;
	}

	if (paused && keys.IsPressed(Key::F7)) {
		return EditorPlayAction::STEP;
	}

	return EditorPlayAction::NONE;
}
} // namespace PlayShortcuts
