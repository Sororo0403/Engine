#pragma once
#include <array>

/// <summary>
/// Canvasまたは親のUI矩形に対する位置・大きさを保持する。Yの正方向は下向き
/// </summary>
struct RectTransformComponent {
	std::array<float, 2> anchorMin{.5f, .5f}, anchorMax{.5f, .5f};
	std::array<float, 2> pivot{.5f, .5f};
	std::array<float, 2> position{};
	// アンカー間の距離に加える大きさ。伸縮指定時は負の値も使用できる
	std::array<float, 2> sizeDelta{240, 60};
	// 矩形の中心を軸に、画面上で時計回りに回転する角度
	float rotationDegrees = 0;

	/// <summary>
	/// アンカー・ピボット・位置・サイズ差分が表現可能か確認する
	/// </summary>
	/// <returns>true:有効 false:非有限、範囲外、またはアンカーの前後が逆</returns>
	bool IsValid() const;
	/// <summary>
	/// 矩形の保存設定が一致するか確認する
	/// </summary>
	/// <param name="other">比較する設定</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const RectTransformComponent &other) const = default;
};
