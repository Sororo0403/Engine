#include "graphics/material/MaterialBindings.h"
#include "platform/windows/PathUtils.h"
#include <optional>

bool MaterialBindings::IsValid() const {
	std::optional<std::uint32_t> previous;

	for (const auto &[slot, path] : slots) {
		if (previous && slot <= *previous) {
			return false;
		}

		previous = slot;
		if (path.empty() || !path.has_filename() ||
		    path.native().find(L'\0') != std::wstring::npos ||
		    !PathUtils::HasExtension(path, ".json") ||
		    !PathUtils::HasExtension(path.stem(), ".material")) {
			return false;
		}
	}

	return true;
}
