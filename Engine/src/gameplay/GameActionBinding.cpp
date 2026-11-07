#include "gameplay/GameActionBinding.h"
#include "platform/windows/StringConversion.h"

bool GameActionBinding::IsValidName(std::string_view name) {
	return !name.empty() && name.size() <= kMaximumNameBytes &&
	       name.find('\0') == std::string_view::npos &&
	       !StringConversion::Utf8ToWide(std::string(name)).empty();
}

bool GameActionBinding::IsValid(std::span<const GameActionBinding> bindings) {
	if (bindings.size() > kMaximumCount)
		return false;
	for (std::size_t i = 0; i < bindings.size(); ++i) {
		const auto &b = bindings[i];
		if (!IsValidName(b.name) ||
		    (b.keyboard && GameInputBindings::GetName(*b.keyboard).empty()) ||
		    (b.mouse &&
		     static_cast<std::size_t>(*b.mouse) >= kMouseOptions.size()) ||
		    (b.gamepad &&
		     static_cast<std::size_t>(*b.gamepad) >= kGamepadOptions.size()) ||
		    (b.gamepadAxis && static_cast<std::size_t>(*b.gamepadAxis) >=
		                          kGamepadAxisOptions.size()))
			return false;
		for (std::size_t j = 0; j < i; ++j) {
			if (b.name == bindings[j].name)
				return false;
		}
	}
	return true;
}
