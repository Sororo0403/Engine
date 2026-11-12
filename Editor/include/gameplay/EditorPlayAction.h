#pragma once

/// <summary>
/// Gameパネルとショートカットから要求する再生操作
/// </summary>
enum class EditorPlayAction {
	NONE,
	PLAY,
	PLAY_STARTUP,
	RESTART,
	STOP,
	STOP_AND_KEEP_TRANSFORMS,
	TOGGLE_PAUSE,
	STEP,
	RENDER_ERROR,
	STOP_AND_SELECT,
};
