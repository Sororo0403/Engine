#include "scene/TextComponent.h"
#include "platform/windows/StringConversion.h"
#include <algorithm>
#include <cmath>

bool TextComponent::IsValid() const {
	return text.size() <= kMaximumTextBytes &&
	       text.find('\0') == std::string::npos &&
	       (text.empty() || !StringConversion::Utf8ToWide(text).empty()) &&
	       !fontFamily.empty() && fontFamily.size() <= 256 &&
	       fontFamily.find('\0') == std::string::npos &&
	       !StringConversion::Utf8ToWide(fontFamily).empty() &&
	       std::isfinite(fontSize) && fontSize >= 1 && fontSize <= 512 &&
	       alignment >= Alignment::LEFT && alignment <= Alignment::RIGHT &&
	       verticalAlignment >= VerticalAlignment::TOP &&
	       verticalAlignment <= VerticalAlignment::BOTTOM &&
	       std::ranges::all_of(color, [](float value) {
		       return std::isfinite(value) && value >= 0 && value <= 1;
	       });
}
