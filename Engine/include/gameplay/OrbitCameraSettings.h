#pragma once

#include <cmath>

/// <summary>
/// Follow Cameraの回転入力と距離変更を設定する。角度は度、距離はワールド単位
/// </summary>
struct OrbitCameraSettings {
	bool enabled = false;
	float mouseSensitivity = 0.2f;
	float stickSpeed = 120.0f;
	float zoomSensitivity = 0.15f;
	float minimumPitch = -75.0f;
	float maximumPitch = 75.0f;
	float minimumDistance = 0.5f;
	float maximumDistance = 100.0f;

	/// <summary>
	/// 感度・回転角・距離の範囲が有限で、上下の極と注視点を避けられるか確認する
	/// </summary>
	/// <returns>true:有効 false:範囲外または大小関係が無効</returns>
	bool IsValid() const {
		return std::isfinite(mouseSensitivity) && mouseSensitivity > 0 &&
		       mouseSensitivity <= 10 && std::isfinite(stickSpeed) &&
		       stickSpeed > 0 && stickSpeed <= 720 &&
		       std::isfinite(zoomSensitivity) && zoomSensitivity > 0 &&
		       zoomSensitivity <= 5 && std::isfinite(minimumPitch) &&
		       std::isfinite(maximumPitch) && minimumPitch >= -89 &&
		       maximumPitch <= 89 && minimumPitch <= maximumPitch &&
		       std::isfinite(minimumDistance) &&
		       std::isfinite(maximumDistance) && minimumDistance >= 0.01f &&
		       maximumDistance <= 10000 && minimumDistance <= maximumDistance;
	}

	/// <summary>
	/// 操作設定と制限が一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const OrbitCameraSettings &other) const = default;
};
