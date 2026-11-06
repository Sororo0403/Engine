#include "animation/AnimatorComponent.h"
#include "platform/windows/StringConversion.h"
#include <cmath>

bool AnimatorComponent::IsValidClip(std::string_view name) {
	return name.size() <= kMaximumClipNameBytes &&
	       name.find('\0') == std::string_view::npos &&
	       (name.empty() ||
	        !StringConversion::Utf8ToWide(std::string(name)).empty());
}

bool AnimatorComponent::IsValid() const {
	return IsValidClip(clip) && std::isfinite(speed) && speed > 0 &&
	       speed <= kMaximumSpeed;
}
