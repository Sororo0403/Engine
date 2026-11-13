#pragma once

/// <summary>
/// Gameパネルとショートカットから要求する再生操作
/// </summary>
enum class EditorPlayAction {
	NONE,
	PLAY,
	PLAY_STARTUP,
	PLAY_LAST_SCENE,
	RESTART,
	RESTART_CURRENT_SCENE,
	STOP,
	STOP_AND_OPEN_SCENE,
	STOP_AND_KEEP_TRANSFORMS,
	TOGGLE_PAUSE,
	STEP,
	RENDER_ERROR,
	STOP_AND_SELECT,
};
