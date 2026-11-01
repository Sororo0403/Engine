#include "physics/BoxColliderUtils.h"

#include "math/MathUtils.h"
#include "math/TransformUtils.h"

#include <algorithm>

bool BoxColliderUtils::IsValid(const BoxCollider &collider) {
	return MathUtils::IsFinite(collider.center) && MathUtils::IsFinite(collider.halfExtents) &&
	       collider.halfExtents.x > 0.0f && collider.halfExtents.y > 0.0f && collider.halfExtents.z > 0.0f;
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
