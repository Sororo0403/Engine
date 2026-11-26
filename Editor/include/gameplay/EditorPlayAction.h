#pragma once

/// <summary>
/// Gameパネルとショートカットから要求する再生操作
/// </summary>
enum class EditorPlayAction {
	NONE,
	PLAY,
	PLAY_STARTUP,
	RESTART,
	RESTART_CURRENT_SCENE,
	STOP,
	STOP_AND_OPEN_SCENE,
	TOGGLE_PAUSE,
	STEP,
	RENDER_ERROR,
	STOP_AND_SELECT,
};
