#pragma once
#include <array>
#include <filesystem>

/// <summary>
/// UI矩形へ表示する画像と色を保持する。画像未割り当ての場合は単色の矩形として描く
/// </summary>
struct ImageComponent {
	bool enabled = true;
	std::filesystem::path texture;
	std::array<float, 4> color{1, 1, 1, 1};
	bool preserveAspect = false;

	/// <summary>
	/// 画像参照の形式とRGBAの範囲を検証する。画像の存在と復号は描画・出力時に確認する
	/// </summary>
	/// <returns>true:有効 false:パス形式・画像拡張子・色が無効</returns>
	bool IsValid() const;
	/// <summary>
	/// 画像参照と保存設定が一致するか確認する
	/// </summary>
	/// <param name="other">比較する設定</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const ImageComponent &other) const = default;
};
