#include "physics/SceneRaycast.h"
#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include "physics/BoxColliderUtils.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
using Vector = std::array<double, 3>;

/// <summary>
/// 3成分を中間計算用の倍精度へ変換する
/// </summary>
Vector Value(const DirectX::XMFLOAT3 &v) {
	return {v.x, v.y, v.z};
}

/// <summary>
/// 行列の線形成分でベクトルを変換し、必要なら移動成分を足す
/// </summary>
Vector TransformVector(const Vector &v, const DirectX::XMFLOAT4X4 &m,
                       bool point) {
	Vector result{};
	for (int axis = 0; axis < 3; ++axis) {
		result[axis] =
		    v[0] * m.m[0][axis] + v[1] * m.m[1][axis] + v[2] * m.m[2][axis];
		if (point)
			result[axis] += m.m[3][axis];
	}
	return result;
}

/// <summary>
/// ローカルの箱との交差区間を求め、距離と外向きのローカル法線を返す
/// </summary>
bool Intersect(const BoxCollider &box, const Vector &origin,
               const Vector &direction, double maximum, double &distance,
               Vector &normal) {
	const auto center = Value(box.center), half = Value(box.halfExtents);
	double enter = -(std::numeric_limits<double>::infinity)();
	double exit = maximum;
	normal = {};
	for (int axis = 0; axis < 3; ++axis) {
		const auto low = center[axis] - half[axis],
		           high = center[axis] + half[axis];
		if (direction[axis] == 0) {
			if (origin[axis] < low || origin[axis] > high)
				return false;
			continue;
		}
		double near = (low - origin[axis]) / direction[axis];
		double far = (high - origin[axis]) / direction[axis];
		const double sign = direction[axis] > 0 ? -1 : 1;
		if (near > far)
			std::swap(near, far);
		if (near > enter) {
			enter = near;
			normal = {};
			normal[axis] = sign;
		}
		exit = (std::min)(exit, far);
		if (enter > exit)
			return false;
	}
	if (exit < 0)
		return false;
	distance = (std::max)(0.0, enter);
	if (enter < 0)
		normal = {};
	return true;
}
} // namespace

bool SceneRaycast::Cast(const Scene &scene, const Ray &ray, RaycastHit &hit,
                        const RaycastOptions &options) {
	try {
		if (!MathUtils::IsFinite(ray.origin) ||
		    !MathUtils::IsFinite(ray.direction) ||
		    !std::isfinite(ray.maxDistance) || ray.maxDistance < 0 ||
		    ray.maxDistance > kMaximumDistance)
			return false;
		auto direction = Value(ray.direction);
		const double length =
		    std::hypot(direction[0], direction[1], direction[2]);
		if (length == 0)
			return false;
		for (auto &component : direction)
			component /= length;
		SceneHierarchy::WorldTransforms transforms;
		SceneActivity::States active;
		if (!SceneHierarchy::BuildWorldTransforms(scene, transforms) ||
		    !SceneActivity::BuildStates(scene, active))
			return false;
		RaycastHit next;
		double closest = ray.maxDistance;
		const auto origin = Value(ray.origin);
		for (const auto &[id, object] : scene.GetObjects()) {
			if (!object.collider || !active.at(id) ||
			    std::ranges::find(options.ignored, id) !=
			        options.ignored.end() ||
			    (!options.includeTriggers && object.collider->isTrigger))
				continue;
			const auto &box = *object.collider;
			if (!BoxColliderUtils::IsValid(box))
				return false;
			if (!(options.layerMask & (std::uint32_t{1} << box.filter.layer)))
				continue;
			DirectX::XMFLOAT4X4 inverse;
			if (!TransformUtils::TryInverseWorldMatrix(transforms.at(id).matrix,
			                                           inverse))
				return false;
			const auto localOrigin = TransformVector(origin, inverse, true);
			const auto localDirection =
			    TransformVector(direction, inverse, false);
			double distance;
			Vector normal;
			if (!Intersect(box, localOrigin, localDirection, closest, distance,
			               normal) ||
			    (next.objectId && distance == closest && id > next.objectId))
				continue;
			if (normal == Vector{}) {
				for (int axis = 0; axis < 3; ++axis)
					normal[axis] = -direction[axis];
			} else {
				Vector world{};
				for (int axis = 0; axis < 3; ++axis)
					world[axis] = normal[0] * inverse.m[axis][0] +
					              normal[1] * inverse.m[axis][1] +
					              normal[2] * inverse.m[axis][2];
				const double magnitude =
				    std::hypot(world[0], world[1], world[2]);
				if (!std::isfinite(magnitude) || magnitude == 0)
					return false;
				for (int axis = 0; axis < 3; ++axis)
					normal[axis] = world[axis] / magnitude;
			}
			next.objectId = id;
			next.distance = static_cast<float>(distance);
			next.position = {
			    static_cast<float>(origin[0] + direction[0] * distance),
			    static_cast<float>(origin[1] + direction[1] * distance),
			    static_cast<float>(origin[2] + direction[2] * distance)};
			next.normal = {static_cast<float>(normal[0]),
			               static_cast<float>(normal[1]),
			               static_cast<float>(normal[2])};
			if (!MathUtils::IsFinite(next.position))
				return false;
			closest = distance;
		}
		hit = next;
		return true;
	} catch (...) {
		return false;
	}
}
