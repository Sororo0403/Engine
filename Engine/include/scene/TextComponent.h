#pragma once
#include <array>
#include <cstddef>
#include <string>

/// <summary>
/// UI矩形へ表示するUTF-8文字列と、フォント・配置・色を保持する
/// </summary>
struct TextComponent {
	enum class Alignment { LEFT, CENTER, RIGHT };
	enum class VerticalAlignment { TOP, MIDDLE, BOTTOM };
	bool enabled = true;
	bool raycastTarget = true;
	std::string text = "Text";
	std::string fontFamily = "M PLUS 1p";
	float fontSize = 32;
	bool bold = false;
	Alignment alignment = Alignment::LEFT;
	VerticalAlignment verticalAlignment = VerticalAlignment::TOP;
	std::array<float, 4> color{1, 1, 1, 1};
	static constexpr std::size_t kMaximumTextBytes = 4096;

	/// <summary>
	/// 文字列・フォント・配置方法と色を検証する
	/// </summary>
	/// <returns>true:有効 false:無効なUTF-8、範囲外、または非有限</returns>
	bool IsValid() const;
	/// <summary>
	/// 表示内容と保存設定が一致するか確認する
	/// </summary>
	/// <param name="other">比較する設定</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const TextComponent &other) const = default;
};
