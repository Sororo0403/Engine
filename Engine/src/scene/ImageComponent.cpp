#include "scene/ImageComponent.h"
#include "platform/windows/PathUtils.h"
#include <algorithm>
#include <cmath>

bool ImageComponent::IsValid() const {
	return std::isfinite(fillAmount) && fillAmount >= 0 && fillAmount <= 1 &&
	       (fillOrigin == FillOrigin::LEFT || fillOrigin == FillOrigin::RIGHT ||
	        fillOrigin == FillOrigin::TOP ||
	        fillOrigin == FillOrigin::BOTTOM) &&
	       (texture.empty() ||
	        (PathUtils::IsImageFile(texture) &&
	         texture.native().find(L'\0') == std::wstring::npos &&
	         (!texture.has_root_path() || texture.is_absolute()))) &&
	       std::ranges::all_of(color, [](float value) {
		       return std::isfinite(value) && value >= 0 && value <= 1;
	       });
}
