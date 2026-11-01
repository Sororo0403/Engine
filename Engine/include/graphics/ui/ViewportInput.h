#pragma once

/// <summary>
/// UI内の画像表示領域に対する操作の可否と入力状態
/// </summary>
struct ViewportInput {
	bool imageHovered = false;
	bool windowFocused = false;
	bool applicationFocused = false;
	bool widgetActive = false;

	// マウスが画像内にあり、正規化位置を取得できたか
	bool imagePositionValid = false;

	// 画像の左上が(0, 0)、右下が(1, 1)
	float imageX = 0.0f;
	float imageY = 0.0f;

	// UIと同じタイミングで取得したマウスボタンの状態
	bool leftPressed = false;
	bool rightPressed = false;
	bool rightDown = false;

	// 縦ホイールの変化量。上方向が正で、通常の1刻みは1。小数も含む
	float wheelDelta = 0.0f;
};
