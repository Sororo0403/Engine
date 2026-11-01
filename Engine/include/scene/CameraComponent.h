#pragma once
#include "graphics/camera/CameraSettings.h"

/// <summary>
/// 物体のTransformを描画カメラとして使用する設定
/// </summary>
struct CameraComponent {
	bool primary = true;
	CameraSettings settings{};
	/// <summary>
	/// 描画設定が有効か確認する。Primaryの重複は再生開始時に確認する
	/// </summary>
	/// <returns>true:有効 false:投影設定が無効</returns>
	bool IsValid() const { return settings.IsValid(); }
	/// <summary>
	/// カメラ設定が一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const CameraComponent &other) const = default;
};
