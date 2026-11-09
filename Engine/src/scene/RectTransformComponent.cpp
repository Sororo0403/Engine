#include "scene/RectTransformComponent.h"
#include <cmath>

bool RectTransformComponent::IsValid() const {
	for (std::size_t i = 0; i < 2; ++i) {
		if (!std::isfinite(anchorMin[i]) || !std::isfinite(anchorMax[i]) ||
		    !std::isfinite(pivot[i]) || anchorMin[i] < 0 || anchorMax[i] > 1 ||
		    anchorMin[i] > anchorMax[i] || pivot[i] < 0 || pivot[i] > 1 ||
		    !std::isfinite(position[i]) || std::abs(position[i]) > 100000 ||
		    !std::isfinite(sizeDelta[i]) || std::abs(sizeDelta[i]) > 100000)
			return false;
	}

	return true;
}
