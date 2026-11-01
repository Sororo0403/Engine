#include "physics/BoxColliderUtils.h"

#include "math/MathUtils.h"
#include "math/TransformUtils.h"

#include <algorithm>
#include <cmath>

bool BoxColliderUtils::IsValid(const BoxCollider &collider) {
	return MathUtils::IsFinite(collider.center) && MathUtils::IsFinite(collider.halfExtents) &&
	       collider.halfExtents.x > 0.0f && collider.halfExtents.y > 0.0f && collider.halfExtents.z > 0.0f;
}

bool BoxColliderUtils::FitToPoints(std::span<const DirectX::XMFLOAT3> points,
                                   BoxCollider &collider, float minimumHalfExtent) {
	if (points.empty() || !std::isfinite(minimumHalfExtent) || minimumHalfExtent <= 0) return false;
	auto minimum = points.front(), maximum = points.front();
	for (const auto &point : points) {
		if (!MathUtils::IsFinite(point)) return false;
		minimum.x = (std::min)(minimum.x, point.x);
		minimum.y = (std::min)(minimum.y, point.y);
		minimum.z = (std::min)(minimum.z, point.z);
		maximum.x = (std::max)(maximum.x, point.x);
		maximum.y = (std::max)(maximum.y, point.y);
		maximum.z = (std::max)(maximum.z, point.z);
	}
	const auto middle = [](float low, float high) { return static_cast<float>((static_cast<double>(low) + high) * 0.5); };
	const auto half = [minimumHalfExtent](float low, float high) {
		return static_cast<float>((std::max)(static_cast<double>(minimumHalfExtent), (static_cast<double>(high) - low) * 0.5));
	};
	BoxCollider candidate{{middle(minimum.x, maximum.x), middle(minimum.y, maximum.y), middle(minimum.z, maximum.z)},
	                      {half(minimum.x, maximum.x), half(minimum.y, maximum.y), half(minimum.z, maximum.z)}};
	if (!IsValid(candidate)) return false;
	collider = candidate;
	return true;
}

bool BoxColliderUtils::GetWorldBounds(const BoxCollider &collider,
                                     const Transform &transform, AxisAlignedBox &bounds) {
	if (!IsValid(collider) || !TransformUtils::IsValid(transform)) {
		return false;
	}
	const auto world = TransformUtils::MakeWorldMatrix(transform);
	AxisAlignedBox candidate;
	for (unsigned int i = 0; i < 8; ++i) {
		const DirectX::XMFLOAT3 local{
		    collider.center.x + ((i & 1) ? collider.halfExtents.x : -collider.halfExtents.x),
		    collider.center.y + ((i & 2) ? collider.halfExtents.y : -collider.halfExtents.y),
		    collider.center.z + ((i & 4) ? collider.halfExtents.z : -collider.halfExtents.z)};
		DirectX::XMFLOAT3 point;
		DirectX::XMStoreFloat3(&point, DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&local), world));
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
	if (candidate.minimum.x >= candidate.maximum.x || candidate.minimum.y >= candidate.maximum.y ||
	    candidate.minimum.z >= candidate.maximum.z) {
		return false;
	}
	bounds = candidate;
	return true;
}
