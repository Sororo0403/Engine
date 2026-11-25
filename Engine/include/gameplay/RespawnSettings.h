#pragma once

#include "math/MathUtils.h"
#include <cmath>

/// <summary>
/// 指定高度より落下した物体を開始位置または指定位置へ戻す設定
/// </summary>
struct RespawnSettings {
	float threshold = -10.0f;
	bool useStartPosition = true;
	DirectX::XMFLOAT3 position{};
	enum class Axis { X, Y, Z };
	Axis axis = Axis::Y;
	bool whenAbove = false;

	float Coordinate(const DirectX::XMFLOAT3 &point) const {
		return axis == Axis::X ? point.x : axis == Axis::Z ? point.z : point.y;
	}

	DirectX::XMFLOAT3 OnBoundary(DirectX::XMFLOAT3 point) const {
		if (axis == Axis::X)
			point.x = threshold;
		else if (axis == Axis::Z)
			point.z = threshold;
		else
			point.y = threshold;
		return point;
	}

	bool HasCrossed(const DirectX::XMFLOAT3 &point) const {
		return whenAbove ? Coordinate(point) > threshold : Coordinate(point) < threshold;
	}

	bool IsSafe(const DirectX::XMFLOAT3 &point) const {
		return whenAbove ? Coordinate(point) < threshold : Coordinate(point) > threshold;
	}

	/// <summary>
	/// 復帰設定が有限か確認する。実際の復帰先の衝突は再生開始時に確認する
	/// </summary>
	/// <returns>true:有効 false:無効</returns>
	bool IsValid() const {
		return (axis == Axis::X || axis == Axis::Y || axis == Axis::Z) &&
		       std::isfinite(threshold) && std::abs(threshold) <= 100000 &&
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
		return threshold == other.threshold && axis == other.axis &&
		       whenAbove == other.whenAbove &&
		       useStartPosition == other.useStartPosition &&
		       position.x == other.position.x &&
		       position.y == other.position.y && position.z == other.position.z;
	}
};
