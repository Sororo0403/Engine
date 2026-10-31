#pragma once

/// <summary>
/// UI内の画像表示領域に対する操作の可否
/// </summary>
struct ViewportInput {
	bool imageHovered = false;
	bool windowFocused = false;
	bool applicationFocused = false;
	bool widgetActive = false;
};
