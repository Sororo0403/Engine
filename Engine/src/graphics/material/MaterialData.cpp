#include "graphics/material/MaterialData.h"
#include "math/MathUtils.h"
#include <cmath>
#include <initializer_list>

bool MaterialData::IsValid() const {
	for (const float value :
	     {baseColor.x, baseColor.y, baseColor.z, baseColor.w}) {
		if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
			return false;
		}
	}

	return MathUtils::IsFinite(uvScale) && MathUtils::IsFinite(uvOffset) &&
	       (shadingMode == ShadingMode::LIT ||
	        shadingMode == ShadingMode::UNLIT);
}

bool MaterialData::operator==(const MaterialData &other) const {
	return baseColor.x == other.baseColor.x &&
	       baseColor.y == other.baseColor.y &&
	       baseColor.z == other.baseColor.z &&
	       baseColor.w == other.baseColor.w && uvScale.x == other.uvScale.x &&
	       uvScale.y == other.uvScale.y && uvOffset.x == other.uvOffset.x &&
	       uvOffset.y == other.uvOffset.y && shadingMode == other.shadingMode &&
	       receiveFog == other.receiveFog;
}
