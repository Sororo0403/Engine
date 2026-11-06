#include "scene/CanvasComponent.h"
#include <cmath>

bool CanvasComponent::IsValid() const {
	return std::isfinite(referenceSize[0]) && std::isfinite(referenceSize[1]) &&
	       referenceSize[0] >= 1 && referenceSize[0] <= 16384 &&
	       referenceSize[1] >= 1 && referenceSize[1] <= 16384 &&
	       std::isfinite(matchWidthOrHeight) && matchWidthOrHeight >= 0 &&
	       matchWidthOrHeight <= 1 && sortOrder >= -10000 && sortOrder <= 10000;
}
