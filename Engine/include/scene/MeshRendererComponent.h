#pragma once

#include "graphics/material/MaterialData.h"
#include "graphics/material/TextureBindings.h"
#include <array>
#include <cmath>

/// <summary>
/// Meshの表示状態・画像スロットの割り当て・UV調整を保持する
/// </summary>
struct MeshRendererComponent {
	bool visible = true;
	TextureBindings textures;

	// 全画像スロットへ適用するUVの倍率と移動量。負の倍率は反転、0は同じ座標の参照に使える
	std::array<float, 2> uvScale{1.0f, 1.0f};
	std::array<float, 2> uvOffset{0.0f, 0.0f};
	RadialReveal reveal;
	bool receiveFog = true;
	MaterialSlotBindings materials;

	/// <summary>
	/// 画像スロットとUVの設定を検証する。Meshが未設定でも保持できる
	/// </summary>
	/// <returns>true:有効 false:画像の参照またはUVが無効</returns>
	bool IsValid() const {
		return materials.IsValid(true) && reveal.IsValid() && textures.IsValid() && std::isfinite(uvScale[0]) &&
		       std::isfinite(uvScale[1]) && std::isfinite(uvOffset[0]) &&
		       std::isfinite(uvOffset[1]);
	}

	/// <summary>
	/// 表示状態・画像の割り当て・UVが一致するか比較する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:相違あり</returns>
	bool operator==(const MeshRendererComponent &other) const = default;
};
