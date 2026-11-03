#include "graphics/material/TextureBindings.h"
#include "platform/windows/PathUtils.h"
#include <optional>

bool TextureBindings::IsValid() const {
	std::optional<std::uint32_t> previous;

	for (const auto &[slot, path] : slots) {
		if (previous && slot <= *previous) {
			return false;
		}

		previous = slot;
		if (path.empty() || !path.has_filename() ||
		    path.native().find(L'\0') != std::wstring::npos ||
		    !PathUtils::IsImageFile(path)) {
			return false;
		}
	}

	return true;
}
