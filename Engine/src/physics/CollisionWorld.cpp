#include "physics/CollisionWorld.h"

#include "math/MathUtils.h"
#include "physics/BoxColliderUtils.h"
#include "scene/Scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
using Vector = std::array<double, 3>;

/// <summary>
/// 座標をdoubleの配列へ変換する
/// </summary>
/// <param name="value">変換する座標</param>
/// <returns>x・y・zの配列</returns>
Vector ToVector(const DirectX::XMFLOAT3 &value) {
	return {value.x, value.y, value.z};
}

/// <summary>
/// ボックスの座標と各軸の大きさを確認する
/// </summary>
/// <param name="box">確認するボックス</param>
/// <returns>true:有効 false:無効</returns>
bool Valid(const AxisAlignedBox &box) {
	return MathUtils::IsFinite(box.minimum) &&
	       MathUtils::IsFinite(box.maximum) && box.minimum.x < box.maximum.x &&
	       box.minimum.y < box.maximum.y && box.minimum.z < box.maximum.z;
}

/// <summary>
/// 開始位置からの移動経路と、障害物の各軸の交差区間を調べる
/// </summary>
/// <param name="minimum">移動するボックスの最小座標</param>
/// <param name="maximum">移動するボックスの最大座標</param>
/// <param name="movement">要求する移動量</param>
/// <param name="obstacle">障害物のボックス</param>
/// <param name="time">交差時刻。0は開始位置、1は移動の終点</param>
/// <param name="axes">接触した軸のビット（X=1、Y=2、Z=4）</param>
/// <returns>true:経路上で接触 false:接触しない</returns>
bool Sweep(const Vector &minimum, const Vector &maximum, const Vector &movement,
           const AxisAlignedBox &obstacle, double &time, unsigned int &axes) {
	const auto low = ToVector(obstacle.minimum),
	           high = ToVector(obstacle.maximum);
	double entry = -(std::numeric_limits<double>::infinity)();
	double exit = (std::numeric_limits<double>::infinity)();
	axes = 0;

	for (std::size_t axis = 0; axis < 3; ++axis) {
		if (movement[axis] == 0.0) {
			if (maximum[axis] <= low[axis] || minimum[axis] >= high[axis]) {
				return false;
			}

			continue;
		}

		const double near = movement[axis] > 0.0
		                        ? (low[axis] - maximum[axis]) / movement[axis]
		                        : (high[axis] - minimum[axis]) / movement[axis];
		const double far = movement[axis] > 0.0
		                       ? (high[axis] - minimum[axis]) / movement[axis]
		                       : (low[axis] - maximum[axis]) / movement[axis];

		if (near > entry) {
			entry = near;
			axes = 1u << axis;
		} else if (near == entry) {
			axes |= 1u << axis;
		}

		exit = (std::min)(exit, far);
	}

	if (entry < 0.0 || entry > 1.0 || entry >= exit) {
		return false;
	}

	time = entry;
	return axes != 0;
}
} // namespace

bool CollisionWorld::Rebuild(const Scene &scene, SceneObjectId ignoredObject) {
	std::vector<AxisAlignedBox> candidate;

	for (const auto &[id, object] : scene.GetObjects()) {
		if (id == ignoredObject || !object.collider) {
			continue;
		}

		AxisAlignedBox bounds;

		if (!BoxColliderUtils::GetWorldBounds(*object.collider,
		                                      object.transform, bounds)) {
			return false;
		}

		candidate.push_back(bounds);
	}

	obstacles_.swap(candidate);
	return true;
}

bool CollisionWorld::CanPlace(const AxisAlignedBox &body) const {
	if (!Valid(body)) {
		return false;
	}

	for (const auto &box : obstacles_) {
		if (body.minimum.x < box.maximum.x && body.maximum.x > box.minimum.x &&
		    body.minimum.y < box.maximum.y && body.maximum.y > box.minimum.y &&
		    body.minimum.z < box.maximum.z && body.maximum.z > box.minimum.z) {
			return false;
		}
	}

	return true;
}

bool CollisionWorld::Move(const AxisAlignedBox &body,
                          const DirectX::XMFLOAT3 &displacement,
                          CollisionMoveResult &result) const {
	if (!CanPlace(body) || !MathUtils::IsFinite(displacement)) {
		return false;
	}

	Vector moved{}, remaining = ToVector(displacement);
	const auto initialMin = ToVector(body.minimum),
	           initialMax = ToVector(body.maximum);
	CollisionMoveResult candidate;

	for (int iteration = 0; iteration < kMaximumSlideIterations; ++iteration) {
		Vector minimum{}, maximum{};

		for (std::size_t axis = 0; axis < 3; ++axis) {
			minimum[axis] = initialMin[axis] + moved[axis];
			maximum[axis] = initialMax[axis] + moved[axis];
		}

		double first = 1.0;
		unsigned int blocked = 0;

		for (const auto &obstacle : obstacles_) {
			double time;
			unsigned int axes;

			if (!Sweep(minimum, maximum, remaining, obstacle, time, axes)) {
				continue;
			}

			if (time < first) {
				first = time;
				blocked = axes;
			} else if (time == first) {
				blocked |= axes;
			}
		}

		if (blocked == 0) {
			for (std::size_t axis = 0; axis < 3; ++axis) {
				moved[axis] += remaining[axis];
			}

			break;
		}

		const double largest =
		    (std::max)({std::abs(remaining[0]), std::abs(remaining[1]),
		                std::abs(remaining[2])});
		const double fraction = (std::max)(0.0, first - kSkinWidth / largest);
		candidate.hitWall |= (blocked & 5u) != 0;
		candidate.grounded |= (blocked & 2u) != 0 && remaining[1] < 0.0;
		candidate.hitCeiling |= (blocked & 2u) != 0 && remaining[1] > 0.0;

		for (std::size_t axis = 0; axis < 3; ++axis) {
			moved[axis] += remaining[axis] * fraction;
			remaining[axis] = (blocked & (1u << axis))
			                      ? 0.0
			                      : remaining[axis] * (1.0 - fraction);
		}
	}

	AxisAlignedBox finalBox;
	auto finalMin = ToVector(body.minimum), finalMax = ToVector(body.maximum);

	for (std::size_t axis = 0; axis < 3; ++axis) {
		finalMin[axis] += moved[axis];
		finalMax[axis] += moved[axis];

		if (std::abs(finalMin[axis]) > (std::numeric_limits<float>::max)() ||
		    std::abs(finalMax[axis]) > (std::numeric_limits<float>::max)()) {
			return false;
		}
	}

	finalBox.minimum = {static_cast<float>(finalMin[0]),
	                    static_cast<float>(finalMin[1]),
	                    static_cast<float>(finalMin[2])};
	finalBox.maximum = {static_cast<float>(finalMax[0]),
	                    static_cast<float>(finalMax[1]),
	                    static_cast<float>(finalMax[2])};

	if (!CanPlace(finalBox)) {
		return false;
	}

	candidate.displacement = {static_cast<float>(moved[0]),
	                          static_cast<float>(moved[1]),
	                          static_cast<float>(moved[2])};
	result = candidate;
	return true;
}
