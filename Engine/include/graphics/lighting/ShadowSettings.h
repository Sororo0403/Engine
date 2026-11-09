#pragma once
#include <cmath>

/// <summary>
/// 平行光源の影の範囲・精度・濃さと、自己遮蔽を抑える補正
/// </summary>
struct ShadowSettings {
	bool enabled = false;
	int resolution = 1024;
	float extent = 30.0f;
	float strength = 0.8f;
	float bias = 0.0005f;
	float normalBias = 0.02f;

	/// <summary>
	/// 解像度と、有限な範囲・濃さ・補正を確認する
	/// </summary>
	/// <returns>true:有効 false:範囲外または非有限</returns>
	bool IsValid() const {
		return (resolution == 512 || resolution == 1024 ||
		        resolution == 2048) &&
		       std::isfinite(extent) && extent >= 1 && extent <= 1000 &&
		       std::isfinite(strength) && strength >= 0 && strength <= 1 &&
		       std::isfinite(bias) && bias >= 0 && bias <= .02f &&
		       std::isfinite(normalBias) && normalBias >= 0 && normalBias <= 1;
	}

	/// <summary>
	/// 影の設定がすべて一致するか確認する
	/// </summary>
	/// <param name="other">比較する設定</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const ShadowSettings &other) const = default;
};
