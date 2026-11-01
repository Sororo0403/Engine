#include "assets/ModelPlacement.h"

#include "math/MathUtils.h"
#include "math/Ray.h"

#include <algorithm>
#include <cmath>

bool ModelPlacement::GetPosition(const Ray &ray, DirectX::XMFLOAT3 &position) {
	if (!MathUtils::IsFinite(ray.origin) || !MathUtils::IsFinite(ray.direction) ||
	    !std::isfinite(ray.maxDistance) || ray.maxDistance <= 0.0f) {
		return false;
	}
	const auto &direction = ray.direction;
	const float lengthSquared = direction.x * direction.x + direction.y * direction.y +
	                            direction.z * direction.z;
	if (!std::isfinite(lengthSquared) || std::abs(lengthSquared - 1.0f) > kDirectionTolerance) {
		return false;
	}
	double distance = (std::min)(kDefaultDistance, ray.maxDistance * 0.5f);
	if (std::abs(direction.y) > kParallelTolerance) {
		const double groundDistance = -static_cast<double>(ray.origin.y) / direction.y;
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
