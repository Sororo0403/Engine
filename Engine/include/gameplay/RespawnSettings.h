#pragma once

#include "math/MathUtils.h"
#include <cmath>

/// <summary>
/// 指定高度より落下した物体を開始位置または指定位置へ戻す設定
/// </summary>
struct RespawnSettings {
	float minimumY = -10.0f;
	bool useStartPosition = true;
	DirectX::XMFLOAT3 position{};

	/// <summary>
	/// 復帰設定が有限か確認する。実際の復帰先の衝突は再生開始時に確認する
	/// </summary>
	/// <returns>true:有効 false:無効</returns>
	bool IsValid() const {
		return std::isfinite(minimumY) && std::abs(minimumY) <= 100000 &&
		       MathUtils::IsFinite(position) &&
		       std::abs(position.x) <= 100000 &&
		       std::abs(position.y) <= 100000 && std::abs(position.z) <= 100000;
	}
	/// <summary>
	/// 設定が一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const RespawnSettings &other) const {
		return minimumY == other.minimumY &&
		       useStartPosition == other.useStartPosition &&
		       position.x == other.position.x &&
		       position.y == other.position.y && position.z == other.position.z;
	}
};
