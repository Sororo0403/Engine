#pragma once
#include <cstddef>
#include <string>
#include <string_view>

/// <summary>
/// Meshが参照するモデル内のクリップを再生する、編集・保存用の設定
/// </summary>
struct AnimatorComponent {
	static constexpr std::size_t kMaximumClipNameBytes = 256;
	static constexpr float kMaximumSpeed = 16;
	// 空はモデル内の最初のクリップを使用する。
	std::string clip;
	float speed = 1;
	bool loop = true;
	bool playOnStart = true;

	/// <summary>
	/// 名前の長さ・UTF-8・NULの有無と、正の有限な速度を確認する
	/// </summary>
	/// <returns>true:設定が有効 false:名前または速度が無効</returns>
	bool IsValid() const;

	/// <summary>
	/// クリップ名として保存・指定できる文字列か確認する
	/// </summary>
	/// <param name="name">モデル内の名前。空は既定クリップ</param>
	/// <returns>true:空または有効なUTF-8 false:上限超過・NUL・不正なUTF-8</returns>
	static bool IsValidClip(std::string_view name);

	/// <summary>
	/// 保存する設定がすべて一致するか確認する
	/// </summary>
	/// <param name="other">比較する設定</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const AnimatorComponent &other) const = default;
};
