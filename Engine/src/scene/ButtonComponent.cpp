#include "scene/ButtonComponent.h"
#include <algorithm>
#include <cmath>

bool ButtonComponent::IsValid() const {
	const auto valid = [](const auto &color) {
		return std::ranges::all_of(color, [](float value) {
			return std::isfinite(value) && value >= 0 && value <= 1;
		});
	};
	return valid(normalColor) && valid(highlightedColor) &&
	       valid(pressedColor) && valid(disabledColor);
}
