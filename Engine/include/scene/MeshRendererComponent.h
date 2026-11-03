#pragma once

#include "graphics/material/TextureBindings.h"

/// <summary>
/// Meshの形状を描画する表示状態と、材質スロットへの割り当てを保持する
/// </summary>
struct MeshRendererComponent {
	bool visible = true;
	TextureBindings textures;

	/// <summary>
	/// 材質スロットの設定を検証する。Meshが未設定でも保持できる
	/// </summary>
	/// <returns>true:有効 false:画像の参照が無効</returns>
	bool IsValid() const {
		return textures.IsValid();
	}

	/// <summary>
	/// 表示状態と画像の割り当てが一致するか比較する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:相違あり</returns>
	bool operator==(const MeshRendererComponent &other) const = default;
};
