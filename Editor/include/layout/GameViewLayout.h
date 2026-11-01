#pragma once

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <optional>

/// <summary>
/// Game画像の縦横比を保って使用可能領域の中央へ配置する
/// </summary>
struct GameViewLayout {
	DirectX::XMFLOAT2 size{};
	DirectX::XMFLOAT2 offset{};

	/// <summary>
	/// 画像全体が収まる表示サイズと左上の余白を計算する
	/// </summary>
	/// <param name="available">ツールバーを除いた使用可能サイズ</param>
	/// <param name="width">ゲームの描画幅。正のピクセル数</param>
	/// <param name="height">ゲームの描画高さ。正のピクセル数</param>
	/// <returns>中央に配置する矩形。領域が非有限または1未満、解像度が不正なら未設定</returns>
	static std::optional<GameViewLayout> Fit(const DirectX::XMFLOAT2 &available,
	                                         int width, int height) {
		if (!std::isfinite(available.x) || !std::isfinite(available.y) ||
		    available.x < 1.0f || available.y < 1.0f || width <= 0 ||
		    height <= 0) {
			return std::nullopt;
		}

		const double scale = (std::min)(double(available.x) / width,
		                                double(available.y) / height);
		GameViewLayout layout;
		layout.size = {float(width * scale), float(height * scale)};
		layout.offset = {(available.x - layout.size.x) * 0.5f,
		                 (available.y - layout.size.y) * 0.5f};
		return layout;
	}
};
