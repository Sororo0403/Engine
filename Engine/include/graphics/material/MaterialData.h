#pragma once

#include <DirectXMath.h>

/// <summary>
/// GPUへ渡す材質の情報
/// </summary>
struct MaterialData {
	// テクスチャの色に掛けるRGBA
	DirectX::XMFLOAT4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};

	// UV座標に掛ける倍率
	DirectX::XMFLOAT2 uvScale{1.0f, 1.0f};

	// UV座標に加える移動量
	DirectX::XMFLOAT2 uvOffset{0.0f, 0.0f};

	/// <summary>
	/// 色が0から1の範囲で、UVの各成分が有限値か確認する
	/// </summary>
	/// <returns>true:有効 false:範囲外または非有限値</returns>
	bool IsValid() const;

	/// <summary>
	/// 全ての材質パラメーターが一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:全成分が一致 false:相違あり</returns>
	bool operator==(const MaterialData &other) const;
};
