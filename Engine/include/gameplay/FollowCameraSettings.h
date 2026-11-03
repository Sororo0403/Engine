#pragma once

#include "math/MathUtils.h"
#include "scene/SceneObjectId.h"
#include <cmath>

/// <summary>
/// 対象の位置に追従するカメラの設定。オフセットはワールド座標で指定する
/// </summary>
struct FollowCameraSettings {
	// オフセットの範囲と、視線が上方向と平行になることを避ける最小水平距離
	static constexpr float kMaximumOffset = 10000.0f;
	static constexpr double kMinimumHorizontalDistanceSquared = 0.000001;
	static constexpr float kMinimumClearance = 0.01f;
	static constexpr float kMaximumClearance = 5.0f;
	static constexpr double kCollisionSkinWidth = 0.0001;

	SceneObjectId target = 0;
	DirectX::XMFLOAT3 offset{0.0f, 3.0f, -7.0f};
	DirectX::XMFLOAT3 lookOffset{0.0f, 1.0f, 0.0f};
	float smoothing = 8.0f;
	bool avoidObstacles = false;
	float clearance = 0.2f;

	/// <summary>
	/// 有限な設定で、視線がゼロまたは上方向と平行にならないか確認する
	/// </summary>
	/// <returns>true:有効 false:無効。対象の存在は再生開始時に確認する</returns>
	bool IsValid() const {
		const double dx = double(offset.x) - lookOffset.x,
		             dz = double(offset.z) - lookOffset.z;
		return MathUtils::IsFinite(offset) && MathUtils::IsFinite(lookOffset) &&
		       std::abs(offset.x) <= kMaximumOffset &&
		       std::abs(offset.y) <= kMaximumOffset &&
		       std::abs(offset.z) <= kMaximumOffset &&
		       std::abs(lookOffset.x) <= kMaximumOffset &&
		       std::abs(lookOffset.y) <= kMaximumOffset &&
		       std::abs(lookOffset.z) <= kMaximumOffset &&
		       dx * dx + dz * dz > kMinimumHorizontalDistanceSquared &&
		       std::isfinite(smoothing) && smoothing >= 0 && smoothing <= 100 &&
		       std::isfinite(clearance) && clearance >= kMinimumClearance &&
		       clearance <= kMaximumClearance;
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
		       smoothing == other.smoothing &&
		       avoidObstacles == other.avoidObstacles &&
		       clearance == other.clearance;
	}
};
