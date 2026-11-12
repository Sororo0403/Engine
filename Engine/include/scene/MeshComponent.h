#pragma once

#include <filesystem>

/// <summary>
/// 描画や材質の設定と独立して、使用するモデルのメッシュ群を参照する
/// </summary>
struct MeshComponent {
	// 実行ファイル基準の相対パス、または絶対パス。空は形状の未割り当て
	std::filesystem::path modelPath;
	// シーンの読み込み時に、非表示や無効な物体のモデルも準備する
	bool preload = false;

	/// <summary>
	/// 形状の参照パスを検証する。空の参照も保持できる
	/// </summary>
	/// <returns>true:有効 false:NULを含む不正なパス</returns>
	bool IsValid() const {
		return modelPath.native().find(L'\0') == std::wstring::npos;
	}

	/// <summary>
	/// 同じ形状の参照を保持しているか比較する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:相違あり</returns>
	bool operator==(const MeshComponent &other) const = default;
};
