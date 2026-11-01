#include "math/MathUtils.h"

#include <cmath>
#include <limits>

bool MathUtils::SnapToGrid(const DirectX::XMFLOAT3 &position,
                          const DirectX::XMFLOAT3 &step, DirectX::XMFLOAT3 &snapped) {
	if (!IsFinite(position) || !IsFinite(step) || step.x <= 0.0f || step.y <= 0.0f || step.z <= 0.0f) {
		return false;
	}
	const auto roundComponent = [](float value, float increment) {
		return std::round(static_cast<double>(value) / increment) * increment;
	};
	const double x = roundComponent(position.x, step.x);
	const double y = roundComponent(position.y, step.y);
	const double z = roundComponent(position.z, step.z);
	const double limit = (std::numeric_limits<float>::max)();
	if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
	    std::abs(x) > limit || std::abs(y) > limit || std::abs(z) > limit) {
		return false;
	}
	snapped = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
	return true;
}

bool MathUtils::IsFinite(const DirectX::XMFLOAT2 &value) {
	return std::isfinite(value.x) && std::isfinite(value.y);
}

bool MathUtils::IsFinite(const DirectX::XMFLOAT3 &value) {
	return std::isfinite(value.x) && std::isfinite(value.y) &&
	       std::isfinite(value.z);
}

bool MathUtils::IsFinite(const DirectX::XMFLOAT4 &value) {
	return std::isfinite(value.x) && std::isfinite(value.y) &&
	       std::isfinite(value.z) && std::isfinite(value.w);
}
