#include "physics/ColliderUtils.h"

#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include "scene/SceneHierarchy.h"

#include <algorithm>
#include <array>
#include <cmath>

bool ColliderUtils::IsValid(const Collider &collider) {
	return collider.IsValid();
}

bool ColliderUtils::FitToPoints(std::span<const DirectX::XMFLOAT3> points,
                                Collider &collider, float minimumHalfExtent) {
	if (points.empty() || !std::isfinite(minimumHalfExtent) ||
	    minimumHalfExtent <= 0)
		return false;

	auto minimum = points.front(), maximum = points.front();

	for (const auto &point : points) {
		if (!MathUtils::IsFinite(point))
			return false;

		minimum.x = (std::min)(minimum.x, point.x);
		minimum.y = (std::min)(minimum.y, point.y);
		minimum.z = (std::min)(minimum.z, point.z);
		maximum.x = (std::max)(maximum.x, point.x);
		maximum.y = (std::max)(maximum.y, point.y);
		maximum.z = (std::max)(maximum.z, point.z);
	}

	const auto middle = [](float low, float high) {
		return static_cast<float>((static_cast<double>(low) + high) * 0.5);
	};
	const auto half = [minimumHalfExtent](float low, float high) {
		return static_cast<float>(
		    (std::max)(static_cast<double>(minimumHalfExtent),
		               (static_cast<double>(high) - low) * 0.5));
	};
	Collider candidate = collider;
	candidate.center = {middle(minimum.x, maximum.x),
	                    middle(minimum.y, maximum.y),
	                    middle(minimum.z, maximum.z)};
	candidate.halfExtents = {half(minimum.x, maximum.x),
	                         half(minimum.y, maximum.y),
	                         half(minimum.z, maximum.z)};
	if (candidate.shape != Collider::Shape::BOX) {
		const auto &h = candidate.halfExtents;
		const double radial = std::hypot(double(h.x), double(h.z));
		candidate.halfHeight = static_cast<float>(
		    (std::max)(double(minimumHalfExtent), double(h.y) - radial));
		double radius = minimumHalfExtent;

		for (const auto &point : points) {
			const double y = std::abs(double(point.y) - candidate.center.y);
			const double vertical =
			    candidate.shape == Collider::Shape::CAPSULE
			        ? (std::max)(0., y - candidate.halfHeight)
			        : y;
			radius =
			    (std::max)(radius,
			               std::hypot(double(point.x) - candidate.center.x,
			                          vertical,
			                          double(point.z) - candidate.center.z));
		}

		candidate.radius = static_cast<float>(radius);
	}

	if (!IsValid(candidate))
		return false;

	collider = candidate;
	return true;
}

bool ColliderUtils::GetWorldBounds(const Collider &collider,
                                   const Transform &transform,
                                   AxisAlignedBox &bounds) {
	if (!IsValid(collider) || !TransformUtils::IsValid(transform)) {
		return false;
	}

	DirectX::XMFLOAT4X4 world;
	DirectX::XMStoreFloat4x4(&world,
	                         TransformUtils::MakeWorldMatrix(transform));
	return GetWorldBounds(collider, world, bounds);
}

bool ColliderUtils::GetWorldBounds(const Collider &collider, const Scene &scene,
                                   SceneObjectId id, AxisAlignedBox &bounds) {
	SceneWorldTransform world;
	return SceneHierarchy::GetWorldTransform(scene, id, world) &&
	       GetWorldBounds(collider, world.matrix, bounds);
}

bool ColliderUtils::GetWorldBounds(const Collider &collider,
                                   const DirectX::XMFLOAT4X4 &matrix,
                                   AxisAlignedBox &bounds) {
	DirectX::XMFLOAT4X4 inverse;

	if (!IsValid(collider) ||
	    !TransformUtils::TryInverseWorldMatrix(matrix, inverse)) {
		return false;
	}

	const auto world = DirectX::XMLoadFloat4x4(&matrix);

	if (collider.shape != Collider::Shape::BOX) {
		std::array<DirectX::XMFLOAT3, 3> axes{
		    {{matrix._11, matrix._12, matrix._13},
		     {matrix._21, matrix._22, matrix._23},
		     {matrix._31, matrix._32, matrix._33}}};
		std::array<double, 3> lengths;
		for (int i = 0; i < 3; ++i)
			lengths[i] = std::hypot(double(axes[i].x), double(axes[i].y),
			                        double(axes[i].z));
		const auto scale = lengths[0];

		for (int i = 0; i < 3; ++i) {
			if (!std::isfinite(lengths[i]) ||
			    std::abs(lengths[i] - scale) > scale * .00001)
				return false;
			for (int j = 0; j < i; ++j) {
				const double dot = double(axes[i].x) * axes[j].x +
				                   double(axes[i].y) * axes[j].y +
				                   double(axes[i].z) * axes[j].z;
				if (std::abs(dot) > lengths[i] * lengths[j] * .00001)
					return false;
			}
		}

		DirectX::XMFLOAT3 center;
		DirectX::XMStoreFloat3(
		    &center, DirectX::XMVector3TransformCoord(
		                 DirectX::XMLoadFloat3(&collider.center), world));
		const double radius = collider.radius * scale;
		const double height = collider.shape == Collider::Shape::CAPSULE
		                          ? collider.halfHeight
		                          : 0;
		const DirectX::XMFLOAT3 extent{
		    static_cast<float>(radius + std::abs(matrix._21) * height),
		    static_cast<float>(radius + std::abs(matrix._22) * height),
		    static_cast<float>(radius + std::abs(matrix._23) * height)};
		AxisAlignedBox next{
		    {center.x - extent.x, center.y - extent.y, center.z - extent.z},
		    {center.x + extent.x, center.y + extent.y, center.z + extent.z}};
		if (!MathUtils::IsFinite(next.minimum) ||
		    !MathUtils::IsFinite(next.maximum) ||
		    next.minimum.x >= next.maximum.x ||
		    next.minimum.y >= next.maximum.y ||
		    next.minimum.z >= next.maximum.z)
			return false;
		bounds = next;
		return true;
	}

	AxisAlignedBox candidate;

	for (unsigned int i = 0; i < 8; ++i) {
		const DirectX::XMFLOAT3 local{
		    collider.center.x +
		        ((i & 1) ? collider.halfExtents.x : -collider.halfExtents.x),
		    collider.center.y +
		        ((i & 2) ? collider.halfExtents.y : -collider.halfExtents.y),
		    collider.center.z +
		        ((i & 4) ? collider.halfExtents.z : -collider.halfExtents.z)};
		DirectX::XMFLOAT3 point;
		DirectX::XMStoreFloat3(&point,
		                       DirectX::XMVector3TransformCoord(
		                           DirectX::XMLoadFloat3(&local), world));

		if (!MathUtils::IsFinite(point)) {
			return false;
		}

		if (i == 0) {
			candidate.minimum = candidate.maximum = point;
		} else {
			candidate.minimum.x = (std::min)(candidate.minimum.x, point.x);
			candidate.minimum.y = (std::min)(candidate.minimum.y, point.y);
			candidate.minimum.z = (std::min)(candidate.minimum.z, point.z);
			candidate.maximum.x = (std::max)(candidate.maximum.x, point.x);
			candidate.maximum.y = (std::max)(candidate.maximum.y, point.y);
			candidate.maximum.z = (std::max)(candidate.maximum.z, point.z);
		}
	}

	if (candidate.minimum.x >= candidate.maximum.x ||
	    candidate.minimum.y >= candidate.maximum.y ||
	    candidate.minimum.z >= candidate.maximum.z) {
		return false;
	}

	bounds = candidate;
	return true;
}
