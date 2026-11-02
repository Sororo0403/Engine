#pragma once

/// <summary>
/// Gameパネルとショートカットから要求する再生操作
/// </summary>
enum class EditorPlayAction {
	NONE,
	PLAY,
	RESTART,
	STOP,
	TOGGLE_PAUSE,
	STEP,
	RENDER_ERROR,
};
