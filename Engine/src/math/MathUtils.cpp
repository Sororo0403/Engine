#include "math/MathUtils.h"

#include <cmath>

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
