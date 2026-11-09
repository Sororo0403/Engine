#include "physics/Collider.h"
#include "math/MathUtils.h"
#include <cmath>

Collider::Collider(const DirectX::XMFLOAT3 &center,
                   const DirectX::XMFLOAT3 &halfExtents,
                   const CollisionFilter &filter, bool isTrigger)
    : center(center), halfExtents(halfExtents), filter(filter),
      isTrigger(isTrigger) {
}

bool Collider::IsValid() const {
	if (!filter.IsValid() || !MathUtils::IsFinite(center) ||
	    !MathUtils::IsFinite(halfExtents) || halfExtents.x <= 0 ||
	    halfExtents.y <= 0 || halfExtents.z <= 0 || !std::isfinite(radius) ||
	    radius <= 0 || !std::isfinite(halfHeight) || halfHeight <= 0)
		return false;
	switch (shape) {
	case Shape::BOX:
		return MathUtils::IsFinite(halfExtents) && halfExtents.x > 0 &&
		       halfExtents.y > 0 && halfExtents.z > 0;
	case Shape::SPHERE:
		return std::isfinite(radius) && radius > 0;
	case Shape::CAPSULE:
		return std::isfinite(radius) && radius > 0 &&
		       std::isfinite(halfHeight) && halfHeight > 0 &&
		       std::isfinite(radius + halfHeight);
	}

	return false;
}

DirectX::XMFLOAT3 Collider::GetHalfExtents() const {
	switch (shape) {
	case Shape::BOX:
		return halfExtents;
	case Shape::SPHERE:
		return {radius, radius, radius};
	case Shape::CAPSULE:
		return {radius, radius + halfHeight, radius};
	}

	return {};
}

bool Collider::operator==(const Collider &other) const {
	return shape == other.shape && center.x == other.center.x &&
	       center.y == other.center.y && center.z == other.center.z &&
	       halfExtents.x == other.halfExtents.x &&
	       halfExtents.y == other.halfExtents.y &&
	       halfExtents.z == other.halfExtents.z && filter == other.filter &&
	       isTrigger == other.isTrigger && radius == other.radius &&
	       halfHeight == other.halfHeight;
}
