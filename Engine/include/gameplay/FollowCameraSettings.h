#pragma once

#include "math/MathUtils.h"
#include "scene/SceneObjectId.h"
#include <cmath>

/// <summary>
/// 対象の位置に追従するカメラの設定。オフセットはワールド座標で指定する
/// </summary>
struct FollowCameraSettings {
	SceneObjectId target = 0;
	DirectX::XMFLOAT3 offset{0.0f, 3.0f, -7.0f};
	DirectX::XMFLOAT3 lookOffset{0.0f, 1.0f, 0.0f};
	float smoothing = 8.0f;

	/// <summary>
	/// 有限な設定で、視線がゼロまたは上方向と平行にならないか確認する
	/// </summary>
	/// <returns>true:有効 false:無効。対象の存在は再生開始時に確認する</returns>
	bool IsValid() const {
		const double dx = double(offset.x) - lookOffset.x,
		             dz = double(offset.z) - lookOffset.z;
		return MathUtils::IsFinite(offset) && MathUtils::IsFinite(lookOffset) &&
		       std::abs(offset.x) <= 10000 && std::abs(offset.y) <= 10000 &&
		       std::abs(offset.z) <= 10000 && std::abs(lookOffset.x) <= 10000 &&
		       std::abs(lookOffset.y) <= 10000 &&
		       std::abs(lookOffset.z) <= 10000 &&
		       dx * dx + dz * dz > 0.000001 && std::isfinite(smoothing) &&
		       smoothing >= 0 && smoothing <= 100;
	}

	/// <summary>
	/// 設定が一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const FollowCameraSettings &other) const {
		return target == other.target && offset.x == other.offset.x &&
		       offset.y == other.offset.y && offset.z == other.offset.z &&
		       lookOffset.x == other.lookOffset.x &&
		       lookOffset.y == other.lookOffset.y &&
		       lookOffset.z == other.lookOffset.z &&
		       smoothing == other.smoothing;
	}
};
