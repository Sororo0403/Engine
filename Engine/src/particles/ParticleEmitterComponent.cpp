#include "particles/ParticleEmitterComponent.h"
#include "platform/windows/PathUtils.h"
#include <algorithm>
#include <cmath>

bool ParticleEmitterComponent::IsValid() const {
	const auto range = [](float v, float a, float b) {
		return std::isfinite(v) && v >= a && v <= b;
	};
	return maxParticles >= 1 && maxParticles <= kMaximumParticles &&
	       burst >= 0 && burst <= kMaximumParticles &&
	       range(duration, .05f, 3600) && range(rate, 0, kMaximumRate) &&
	       range(lifetimeMin, .01f, kMaximumLifetime) &&
	       range(lifetimeMax, lifetimeMin, kMaximumLifetime) &&
	       range(speedMin, 0, 1000) && range(speedMax, speedMin, 1000) &&
	       range(radius, 0, 1000) && range(coneDegrees, 0, 180) &&
	       range(sizeStart, 0, 1000) && range(sizeEnd, 0, 1000) &&
	       range(rotationSpeed, -720, 720) && range(gravity.x, -1000, 1000) &&
	       range(gravity.y, -1000, 1000) && range(gravity.z, -1000, 1000) &&
	       std::ranges::all_of(colorStart,
	                           [&](float v) { return range(v, 0, 1); }) &&
	       std::ranges::all_of(colorEnd,
	                           [&](float v) { return range(v, 0, 1); }) &&
	       (texture.empty() ||
	        (PathUtils::IsImageFile(texture) &&
	         texture.native().find(L'\0') == std::wstring::npos &&
	         (!texture.has_root_path() || texture.is_absolute())));
}

bool ParticleEmitterComponent::operator==(
    const ParticleEmitterComponent &o) const {
	return playOnStart == o.playOnStart && loop == o.loop &&
	       localSpace == o.localSpace && additive == o.additive &&
	       texture == o.texture && seed == o.seed &&
	       maxParticles == o.maxParticles && burst == o.burst &&
	       duration == o.duration && rate == o.rate &&
	       lifetimeMin == o.lifetimeMin && lifetimeMax == o.lifetimeMax &&
	       speedMin == o.speedMin && speedMax == o.speedMax &&
	       radius == o.radius && coneDegrees == o.coneDegrees &&
	       sizeStart == o.sizeStart && sizeEnd == o.sizeEnd &&
	       rotationSpeed == o.rotationSpeed && gravity.x == o.gravity.x &&
	       gravity.y == o.gravity.y && gravity.z == o.gravity.z &&
	       colorStart == o.colorStart && colorEnd == o.colorEnd;
}
