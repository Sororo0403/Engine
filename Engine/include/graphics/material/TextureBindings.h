#pragma once

#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

/// <summary>
/// モデルの材質スロット番号と、画像テクスチャの割り当て
/// </summary>
struct TextureBindings {
	// 番号の昇順に保持する。未登録のスロットは、白いテクスチャを使う
	std::vector<std::pair<std::uint32_t, std::filesystem::path>> slots;

	/// <summary>
	/// 全割り当ての参照パスを検証する。ファイルの実在確認は読み込み時に行う
	/// </summary>
	/// <returns>true:有効 false:空または無効なパスあり</returns>
	bool IsValid() const;

	/// <summary>
	/// 画像の割り当てが一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:相違あり</returns>
	bool operator==(const TextureBindings &other) const = default;
};
