#pragma once
#include <array>

/// <summary>
/// UI矩形へのポインター操作・決定操作と、状態ごとの表示色を設定する
/// </summary>
struct ButtonComponent {
	bool enabled = true;
	bool interactable = true;
	bool navigation = true;
	std::array<float, 4> normalColor{1, 1, 1, 1};
	std::array<float, 4> highlightedColor{.85f, .85f, 1, 1};
	std::array<float, 4> pressedColor{.6f, .6f, .8f, 1};
	std::array<float, 4> disabledColor{.45f, .45f, .45f, 1};

	/// <summary>
	/// 各状態のRGBAが有限で0から1の範囲か確認する
	/// </summary>
	/// <returns>true:有効 false:色が範囲外または非有限</returns>
	bool IsValid() const;
	/// <summary>
	/// 操作の可否と表示色が一致するか確認する
	/// </summary>
	/// <param name="other">比較する設定</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const ButtonComponent &other) const = default;
};
