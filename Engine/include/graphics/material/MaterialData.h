#pragma once

#include <DirectXMath.h>
#include <array>
#include <cstdint>

/// <summary>
/// ワールド座標を中心に、球の内側へ向けて材質の色を表示する
/// </summary>
struct RadialReveal {
	std::array<float, 4> originRadius{};
	// 半径の前後に取る幅。0なら効果を無効にする
	float feather = 0;
	bool IsValid() const;
	bool operator==(const RadialReveal &) const = default;
};

/// <summary>
/// 明度を低・高の2段階へ寄せる白黒効果
/// </summary>
struct MonochromeEffect {
	float amount = 0;
	float low = .035f;
	float high = .22f;
	float threshold = .42f;
	float softness = .055f;
	bool IsValid() const;
	bool operator==(const MonochromeEffect &) const = default;
};

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
	// HLSLの定数バッファを16バイト境界へ合わせる予約領域
	std::uint32_t padding[3]{};
	RadialReveal reveal;
	std::uint32_t revealPadding[3]{};
	MonochromeEffect monochrome;
	std::uint32_t monochromePadding[3]{};

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
