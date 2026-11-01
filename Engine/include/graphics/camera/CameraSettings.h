#pragma once
#include <cmath>

/// <summary>
/// 透視投影の縦視野角と表示距離。視野角は度数、距離はワールド座標を使用する
/// </summary>
struct CameraSettings {
	float verticalFovDegrees = 45.0f;
	float nearClip = 0.1f;
	float farClip = 100.0f;
	/// <summary>
	/// 有限で有効な透視投影を構成できるか確認する
	/// </summary>
	/// <returns>true:有効 false:範囲外</returns>
	bool IsValid() const {
		return std::isfinite(verticalFovDegrees) && verticalFovDegrees >= 1 &&
		       verticalFovDegrees <= 179 && std::isfinite(nearClip) &&
		       nearClip >= 0.001f && std::isfinite(farClip) &&
		       farClip > nearClip && farClip <= 100000;
	}
	/// <summary>
	/// 投影設定が一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const CameraSettings &other) const = default;
};
