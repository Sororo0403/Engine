#pragma once

#include <cmath>

/// <summary>
/// 追従カメラの1フレームの操作。マウス移動量とスティックの速度入力を分けて保持する
/// </summary>
struct OrbitCameraInput {
	float mouseX = 0, mouseY = 0;
	float stickX = 0, stickY = 0;
	// 正は近づく、負は離れる。ホイール1目盛りを1とする。
	float zoom = 0;
	bool reset = false;

	/// <summary>
	/// 相対移動とズームが有限で、スティックが正規化済みの範囲か確認する
	/// </summary>
	/// <returns>true:有効 false:非有限値または範囲外</returns>
	bool IsValid() const {
		return std::isfinite(mouseX) && std::isfinite(mouseY) &&
		       std::isfinite(stickX) && std::isfinite(stickY) &&
		       std::isfinite(zoom) && std::abs(stickX) <= 1 &&
		       std::abs(stickY) <= 1;
	}
};
