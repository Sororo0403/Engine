#include "graphics/material/TextureBindings.h"
#include "platform/windows/PathUtils.h"
#include "graphics/material/MaterialAsset.h"
#include <optional>

bool MaterialSlotBindings::IsValid(bool materialAssets) const {
	std::optional<std::uint32_t> previous;

	for (const auto &[slot, path] : slots) {
		if (previous && slot <= *previous) {
			return false;
		}

		previous = slot;
		if (path.empty() || !path.has_filename() ||
		    path.native().find(L'\0') != std::wstring::npos ||
		    !(materialAssets ? MaterialSerializer::IsPath(path)
		                     : PathUtils::IsImageFile(path))) {
			return false;
		}
	}

	return true;
}
