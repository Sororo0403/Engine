#pragma once

#include <DirectXMath.h>
#include <array>
#include <cstdint>

/// <summary>
/// GPUへ渡す材質の情報
/// </summary>
struct MaterialData {
	/// <summary>
	/// 材質が照明の影響を受けるかを指定する。GPU側と同じ32ビット値を使用する
	/// </summary>
	enum class ShadingMode : std::uint32_t {
		LIT = 0,
		UNLIT = 1,
	};

	// テクスチャの色に掛けるRGBA
	DirectX::XMFLOAT4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};

	// UV座標に掛ける倍率
	DirectX::XMFLOAT2 uvScale{1.0f, 1.0f};

	// UV座標に加える移動量
	DirectX::XMFLOAT2 uvOffset{0.0f, 0.0f};

	ShadingMode shadingMode = ShadingMode::LIT;
	std::uint32_t receiveFog = 1;
	// HLSLの定数バッファを16バイト境界へ合わせる予約領域
	std::uint32_t padding[2]{};

	/// <summary>
	/// 色・UV・照明方式が有効か確認する
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
