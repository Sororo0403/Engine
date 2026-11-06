#pragma once

/// <summary>
/// ゲーム内UIへ渡す、描画先に正規化したポインターと選択・決定操作
/// </summary>
struct UIInput {
	int width = 1280, height = 720;
	bool enabled = false;
	bool navigationEnabled = false;
	bool pointerValid = false;
	float pointerX = 0, pointerY = 0;
	bool pointerPressed = false, pointerDown = false;
	bool next = false, previous = false;
	int horizontal = 0, vertical = 0;
	bool submitPressed = false, submitDown = false;

	/// <summary>
	/// 描画寸法・ポインター位置・選択方向が表現可能か確認する
	/// </summary>
	/// <returns>true:有効 false:サイズ・座標・方向が範囲外</returns>
	bool IsValid() const;
};
