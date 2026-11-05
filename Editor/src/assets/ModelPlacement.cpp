#include "assets/ModelPlacement.h"
#include "scene/SceneActivity.h"

#include "math/MathUtils.h"
#include "math/Ray.h"
#include "physics/BoxColliderUtils.h"
#include "scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <limits>

bool ModelPlacement::GetGroundTranslation(
    std::span<const std::array<DirectX::XMFLOAT3, 8>> bounds,
    DirectX::XMFLOAT3 &translation) {
	if (bounds.empty()) {
		return false;
	}

	float bottom = bounds.front().front().y;

	for (const auto &box : bounds) {
		for (const auto &corner : box) {
			if (!MathUtils::IsFinite(corner)) {
				return false;
			}

			bottom = (std::min)(bottom, corner.y);
		}
	}

	translation = {0.0f, std::abs(bottom) <= kGroundTolerance ? 0.0f : -bottom,
	               0.0f};
	return true;
}

bool ModelPlacement::GetPosition(const Ray &ray, DirectX::XMFLOAT3 &position,
                                 float planeHeight) {
	if (!std::isfinite(planeHeight) || !MathUtils::IsFinite(ray.origin) ||
	    !MathUtils::IsFinite(ray.direction) ||
	    !std::isfinite(ray.maxDistance) || ray.maxDistance <= 0.0f) {
		return false;
	}

	const auto &direction = ray.direction;
	const float lengthSquared = direction.x * direction.x +
	                            direction.y * direction.y +
	                            direction.z * direction.z;

	if (!std::isfinite(lengthSquared) ||
	    std::abs(lengthSquared - 1.0f) > kDirectionTolerance) {
		return false;
	}

	double distance = (std::min)(kDefaultDistance, ray.maxDistance * 0.5f);

	if (std::abs(direction.y) > kParallelTolerance) {
		const double groundDistance =
		    (double(planeHeight) - ray.origin.y) / direction.y;

		if (groundDistance >= 0.0 && groundDistance <= ray.maxDistance) {
			distance = groundDistance;
		}
	}

	const DirectX::XMFLOAT3 result{
	    static_cast<float>(ray.origin.x + direction.x * distance),
	    static_cast<float>(ray.origin.y + direction.y * distance),
	    static_cast<float>(ray.origin.z + direction.z * distance),
	};

	if (!MathUtils::IsFinite(result)) {
		return false;
	}

	position = result;
	return true;
}

namespace {
/// <summary>
/// ボックスとの最初の交点と外向き法線を求める。内部からは出口の面を使用する
/// </summary>
bool IntersectSurface(const Ray &ray, const AxisAlignedBox &box,
                      double &distance, DirectX::XMFLOAT3 &normal) {
	const std::array<double, 3> origin{ray.origin.x, ray.origin.y,
	                                   ray.origin.z};
	const std::array<double, 3> direction{ray.direction.x, ray.direction.y,
	                                      ray.direction.z};
	const std::array<double, 3> low{box.minimum.x, box.minimum.y,
	                                box.minimum.z};
	const std::array<double, 3> high{box.maximum.x, box.maximum.y,
	                                 box.maximum.z};
	double enter = -std::numeric_limits<double>::infinity();
	double leave = std::numeric_limits<double>::infinity();
	std::array<float, 3> enterNormal{}, leaveNormal{};

	// 角に同時に当たる場合は床となるY面を優先する。
	for (const auto axis : {1, 0, 2}) {
		if (direction[axis] == 0.0) {
			if (origin[axis] < low[axis] || origin[axis] > high[axis]) {
				return false;
			}

			continue;
		}

		double first = (low[axis] - origin[axis]) / direction[axis];
		double last = (high[axis] - origin[axis]) / direction[axis];
		float sign = -1.0f;

		if (first > last) {
			std::swap(first, last);
			sign = 1.0f;
		}

		if (first > enter) {
			enter = first;
			enterNormal = {};
			enterNormal[axis] = sign;
		}

		if (last < leave) {
			leave = last;
			leaveNormal = {};
			leaveNormal[axis] = -sign;
		}

		if (enter > leave) {
			return false;
		}
	}

	distance = enter >= 0.0 ? enter : leave;

	if (distance < 0.0 || distance > ray.maxDistance) {
		return false;
	}

	const auto &face = enter >= 0.0 ? enterNormal : leaveNormal;
	normal = {face[0], face[1], face[2]};
	return true;
}
} // namespace

bool ModelPlacement::GetSurface(const Ray &ray, const Scene &scene,
                                Surface &surface, float planeHeight) {
	Surface result;

	if (!GetPosition(ray, result.position, planeHeight)) {
		return false;
	}

	if (std::abs(ray.direction.y) > kParallelTolerance) {
		const double ground =
		    (double(planeHeight) - ray.origin.y) / ray.direction.y;
		result.onSurface = ground >= 0.0 && ground <= ray.maxDistance;

		if (result.onSurface) {
			result.position.y = planeHeight;
			result.normal = {0.0f, 1.0f, 0.0f};
		}
	}

	double closest = std::numeric_limits<double>::infinity();

	for (const auto &[id, object] : scene.GetObjects()) {
		AxisAlignedBox box;

		if (!SceneActivity::IsActive(scene, id) || !object.collider ||
		    !BoxColliderUtils::GetWorldBounds(*object.collider, scene, id,
		                                      box)) {
			continue;
		}

		double distance;
		DirectX::XMFLOAT3 normal;

		if (!IntersectSurface(ray, box, distance, normal) ||
		    distance >= closest) {
			continue;
		}

		closest = distance;
		result.position = {
		    static_cast<float>(ray.origin.x + ray.direction.x * distance),
		    static_cast<float>(ray.origin.y + ray.direction.y * distance),
		    static_cast<float>(ray.origin.z + ray.direction.z * distance)};
		// 面に垂直な成分はレイの丸め誤差を含めず、ボックスの面と一致させる。
		if (normal.x != 0.0f) {
			result.position.x = normal.x > 0.0f ? box.maximum.x : box.minimum.x;
		} else if (normal.y != 0.0f) {
			result.position.y = normal.y > 0.0f ? box.maximum.y : box.minimum.y;
		} else {
			result.position.z = normal.z > 0.0f ? box.maximum.z : box.minimum.z;
		}

		result.normal = normal;
		result.onSurface = true;
	}

	if (!MathUtils::IsFinite(result.position)) {
		return false;
	}

	surface = result;
	return true;
}

bool ModelPlacement::GetSurfaceTranslation(
    const Surface &surface, const std::array<DirectX::XMFLOAT3, 8> &bounds,
    DirectX::XMFLOAT3 &translation) {
	if (!MathUtils::IsFinite(surface.position) ||
	    !MathUtils::IsFinite(surface.normal)) {
		return false;
	}

	for (const auto &point : bounds) {
		if (!MathUtils::IsFinite(point)) {
			return false;
		}
	}

	if (!surface.onSurface) {
		translation = surface.position;
		return true;
	}

	const std::array<float, 3> n{surface.normal.x, surface.normal.y,
	                             surface.normal.z};
	const std::array<float, 3> p{surface.position.x, surface.position.y,
	                             surface.position.z};
	std::size_t axis = 0;

	while (axis < n.size() && n[axis] == 0.0f) {
		++axis;
	}

	if (axis == n.size() || std::abs(n[axis]) != 1.0f) {
		return false;
	}

	for (std::size_t i = axis + 1; i < n.size(); ++i) {
		if (n[i] != 0.0f) {
			return false;
		}
	}

	double support = std::numeric_limits<double>::infinity();
	double opposite = -std::numeric_limits<double>::infinity();

	for (const auto &point : bounds) {
		const std::array<float, 3> value{point.x, point.y, point.z};
		support =
		    (std::min)(support, static_cast<double>(value[axis]) * n[axis]);
		opposite =
		    (std::max)(opposite, static_cast<double>(value[axis]) * n[axis]);
	}

	auto result = p;
	const double target = p[axis] + (kSurfaceClearance - support) * n[axis];
	result[axis] = static_cast<float>(target);

	// 移動成分の丸めが面の内側へ向く場合は、外側の隣接値を選ぶ。
	if ((static_cast<double>(result[axis]) - target) * n[axis] < 0.0) {
		result[axis] = std::nextafter(
		    result[axis], n[axis] * std::numeric_limits<float>::infinity());
	}

	const DirectX::XMFLOAT3 position{result[0], result[1], result[2]};

	if (!MathUtils::IsFinite(position)) {
		return false;
	}

	// 実際のfloat加算でも面の内側へ入り込まず、接する位置を表現できることを確認する。
	double gap = std::numeric_limits<double>::infinity();

	for (const auto &point : bounds) {
		const std::array<float, 3> value{point.x, point.y, point.z};
		const float world = value[axis] + result[axis];

		if (!std::isfinite(world)) {
			return false;
		}

		gap = (std::min)(gap, (static_cast<double>(world) - p[axis]) * n[axis]);
	}

	const double tolerance =
	    kSurfaceClearance +
	    kRoundoffFactor * std::numeric_limits<float>::epsilon() *
	        (std::max)({1.0, std::abs(static_cast<double>(p[axis])),
	                    opposite - support});

	if (gap < 0.0 || gap > tolerance) {
		return false;
	}

	translation = position;
	return true;
}
