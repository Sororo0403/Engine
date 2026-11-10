#pragma once
#include <array>

/// <summary>
/// 画面用Canvasの基準解像度・拡縮と描画順を保持する
/// </summary>
struct CanvasComponent {
	bool enabled = true;
	bool scaleWithScreen = true;
	std::array<float, 2> referenceSize{1280, 720};
	float matchWidthOrHeight = .5f;
	int sortOrder = 0;
	// 背景Canvasは3Dより先に描画し、UI入力の対象から外す
	bool background = false;

	/// <summary>
	/// 解像度・幅と高さの比重・描画順が範囲内か確認する
	/// </summary>
	/// <returns>true:有効 false:範囲外または非有限</returns>
	bool IsValid() const;
	/// <summary>
	/// Canvasの保存設定が一致するか確認する
	/// </summary>
	/// <param name="other">比較する設定</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const CanvasComponent &other) const = default;
};
