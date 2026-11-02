#pragma once

#include "math/MathUtils.h"

/// <summary>
/// モデルの元の材質へ掛けるRGB。透明度・テクスチャ・UV設定は変更しない
/// </summary>
struct ColorTint {
	DirectX::XMFLOAT3 color{1.0f, 1.0f, 1.0f};

	/// <summary>
	/// RGBの全成分が有限で0から1の範囲にあるか確認する
	/// </summary>
	/// <returns>true:有効 false:無効</returns>
	bool IsValid() const {
		return MathUtils::IsFinite(color) && color.x >= 0.0f &&
		       color.x <= 1.0f && color.y >= 0.0f && color.y <= 1.0f &&
		       color.z >= 0.0f && color.z <= 1.0f;
	}

	/// <summary>
	/// RGBの設定が一致するか確認する
	/// </summary>
	/// <param name="other">比較する設定</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const ColorTint &other) const {
		return color.x == other.color.x && color.y == other.color.y &&
		       color.z == other.color.z;
	}
};
