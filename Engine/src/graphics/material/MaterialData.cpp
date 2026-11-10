#include "graphics/material/MaterialData.h"
#include "math/MathUtils.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>

bool RadialReveal::IsValid() const {
	return std::isfinite(feather) && feather >= 0 && feather <= 100000 &&
	       std::ranges::all_of(originRadius, [](float value) {
		       return std::isfinite(value) && std::abs(value) <= 100000;
	       });
}

bool MonochromeEffect::IsValid() const {
	return amount >= 0 && amount <= 1 && low >= 0 && low <= high &&
	       high <= 1 && threshold >= 0 && threshold <= 1 &&
	       softness >= .0001f && softness <= 1;
}

bool MaterialData::IsValid() const {
	for (const float value :
	     {baseColor.x, baseColor.y, baseColor.z, baseColor.w}) {
		if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
			return false;
		}
	}

	return reveal.IsValid() && monochrome.IsValid() &&
	       MathUtils::IsFinite(uvScale) && MathUtils::IsFinite(uvOffset) &&
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
	       reveal == other.reveal && monochrome == other.monochrome;
}
