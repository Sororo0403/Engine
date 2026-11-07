#include "physics/SceneRaycast.h"
#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include "physics/ColliderUtils.h"
#include "physics/WorldCollider.h"
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
bool IntersectBox(const Collider &box, const Vector &origin,
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

/// <summary>
/// 球・Y軸カプセルの実表面と交差する最も近いレイ位置を、ローカル座標で調べる
/// </summary>
bool IntersectRound(const Collider &shape, const Vector &origin,
                    const Vector &direction, double maximum, double &distance,
                    Vector &normal) {
	const Vector o{origin[0] - shape.center.x, origin[1] - shape.center.y,
	               origin[2] - shape.center.z};
	const double radius = shape.radius,
	             halfHeight = shape.shape == Collider::Shape::CAPSULE
	                              ? shape.halfHeight
	                              : 0;
	const double y = o[1] - (std::clamp)(o[1], -halfHeight, halfHeight);
	if (o[0] * o[0] + y * y + o[2] * o[2] <= radius * radius) {
		distance = 0;
		normal = {};
		return true;
	}
	bool hit = false;
	double closest = maximum;
	const auto accept = [&](double t, const Vector &n) {
		if (t >= 0 && t <= closest) {
			hit = true;
			closest = t;
			normal = n;
		}
	};
	const auto sphere = [&](double centerY, int cap) {
		const Vector p{o[0], o[1] - centerY, o[2]};
		const double a = direction[0] * direction[0] +
		                 direction[1] * direction[1] +
		                 direction[2] * direction[2];
		const double b =
		    p[0] * direction[0] + p[1] * direction[1] + p[2] * direction[2];
		const Vector cross{p[1] * direction[2] - p[2] * direction[1],
		                   p[2] * direction[0] - p[0] * direction[2],
		                   p[0] * direction[1] - p[1] * direction[0]};
		const double discriminant = a * radius * radius - cross[0] * cross[0] -
		                            cross[1] * cross[1] - cross[2] * cross[2];
		if (discriminant < 0)
			return;
		const double root = std::sqrt(discriminant);
		for (const double t : {(-b - root) / a, (-b + root) / a}) {
			const double worldY = o[1] + t * direction[1];
			if ((cap > 0 && worldY < halfHeight) ||
			    (cap < 0 && worldY > -halfHeight))
				continue;
			accept(t, {p[0] + t * direction[0], p[1] + t * direction[1],
			           p[2] + t * direction[2]});
		}
	};
	if (halfHeight == 0)
		sphere(0, 0);
	else {
		const double a =
		    direction[0] * direction[0] + direction[2] * direction[2];
		if (a > 0) {
			const double b = o[0] * direction[0] + o[2] * direction[2];
			const double cross = o[0] * direction[2] - o[2] * direction[0];
			const double discriminant = a * radius * radius - cross * cross;
			if (discriminant >= 0) {
				const double root = std::sqrt(discriminant);
				for (const double t : {(-b - root) / a, (-b + root) / a}) {
					const double localY = o[1] + t * direction[1];
					if (localY >= -halfHeight && localY <= halfHeight)
						accept(t, {o[0] + t * direction[0], 0,
						           o[2] + t * direction[2]});
				}
			}
		}
		sphere(halfHeight, 1);
		sphere(-halfHeight, -1);
	}
	if (hit)
		distance = closest;
	return hit;
}

bool Intersect(const Collider &shape, const Vector &origin,
               const Vector &direction, double maximum, double &distance,
               Vector &normal) {
	return shape.shape == Collider::Shape::BOX
	           ? IntersectBox(shape, origin, direction, maximum, distance,
	                          normal)
	           : IntersectRound(shape, origin, direction, maximum, distance,
	                            normal);
}

/// <summary>
/// 距離の範囲を検証し、有限な方向を倍精度で単位化する
/// </summary>
bool Normalize(const Ray &ray, Vector &direction) {
	if (!MathUtils::IsFinite(ray.origin) ||
	    !MathUtils::IsFinite(ray.direction) ||
	    !std::isfinite(ray.maxDistance) || ray.maxDistance < 0 ||
	    ray.maxDistance > SceneRaycast::kMaximumDistance)
		return false;
	direction = Value(ray.direction);
	const double length = std::hypot(direction[0], direction[1], direction[2]);
	if (length == 0)
		return false;
	for (auto &component : direction)
		component /= length;
	return true;
}
} // namespace

bool SceneRaycast::Cast(const Scene &scene, const Ray &ray, RaycastHit &hit,
                        const RaycastOptions &options) {
	try {
		Vector direction;
		if (!Normalize(ray, direction))
			return false;
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
			if (!ColliderUtils::IsValid(box))
				return false;
			if (box.shape != Collider::Shape::BOX) {
				AxisAlignedBox bounds;
				if (!ColliderUtils::GetWorldBounds(
				        box, transforms.at(id).matrix, bounds))
					return false;
			}
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

bool SceneRaycast::CastSphere(const Scene &scene, const Ray &ray, float radius,
                              RaycastHit &hit, const RaycastOptions &options) {
	try {
		Vector direction;
		if (!Normalize(ray, direction))
			return false;
		Collider sphere;
		sphere.shape = Collider::Shape::SPHERE;
		sphere.radius = radius;
		DirectX::XMFLOAT4X4 matrix;
		DirectX::XMStoreFloat4x4(
		    &matrix, DirectX::XMMatrixTranslation(ray.origin.x, ray.origin.y,
		                                          ray.origin.z));
		WorldCollider query;
		std::string error;
		if (!WorldCollider::Create(sphere, matrix, query, error))
			return false;
		SceneHierarchy::WorldTransforms transforms;
		SceneActivity::States active;
		if (!SceneHierarchy::BuildWorldTransforms(scene, transforms) ||
		    !SceneActivity::BuildStates(scene, active))
			return false;
		const auto origin = Value(ray.origin);
		const DirectX::XMFLOAT3 delta{float(direction[0] * ray.maxDistance),
		                              float(direction[1] * ray.maxDistance),
		                              float(direction[2] * ray.maxDistance)};
		const auto displacement = Value(delta);
		const DirectX::XMFLOAT3 backward{
		    float(-direction[0]), float(-direction[1]), float(-direction[2])};
		RaycastHit next;
		double closest = 1;
		for (const auto &[id, object] : scene.GetObjects()) {
			if (!object.collider || !active.at(id) ||
			    std::ranges::find(options.ignored, id) !=
			        options.ignored.end() ||
			    (!options.includeTriggers && object.collider->isTrigger))
				continue;
			const auto &collider = *object.collider;
			AxisAlignedBox bounds;
			const auto &pose = transforms.at(id).matrix;
			if (!ColliderUtils::GetWorldBounds(collider, pose, bounds))
				return false;
			if (!(options.layerMask &
			      (std::uint32_t{1} << collider.filter.layer)))
				continue;
			const auto lower = Value(bounds.minimum),
			           upper = Value(bounds.maximum);
			bool near = true;
			for (int axis = 0; axis < 3; ++axis) {
				const double from = origin[axis],
				             to = from + displacement[axis];
				const double padding = radius + .00001;
				near &= (std::min)(from, to) - padding <= upper[axis] &&
				        (std::max)(from, to) + padding >= lower[axis];
			}
			if (!near)
				continue;
			WorldCollider target;
			bool overlapping = false;
			if (!WorldCollider::Create(collider, pose, target, error) ||
			    !query.Overlaps(target, -.000001f, overlapping))
				return false;
			std::optional<ColliderCastHit> contact;
			if (overlapping)
				contact = ColliderCastHit{0, backward};
			else if (ray.maxDistance > 0 && !query.Cast(target, delta, contact))
				return false;
			if (!contact || contact->fraction > closest ||
			    (next.objectId && contact->fraction == closest &&
			     id > next.objectId))
				continue;
			closest = contact->fraction;
			next = {id,
			        float(closest * ray.maxDistance),
			        {float(origin[0] + displacement[0] * closest),
			         float(origin[1] + displacement[1] * closest),
			         float(origin[2] + displacement[2] * closest)},
			        contact->normal};
		}
		hit = next;
		return true;
	} catch (...) {
		return false;
	}
}
