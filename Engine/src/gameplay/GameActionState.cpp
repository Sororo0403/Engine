#include "gameplay/GameActionState.h"

bool GameActionState::IsValid(std::span<const GameActionState> states) {
	if (states.size() > GameActionBinding::kMaximumCount)
		return false;
	for (std::size_t i = 0; i < states.size(); ++i) {
		const auto &s = states[i];
		if (!GameActionBinding::IsValidName(s.name) || (s.pressed && !s.down) ||
		    (s.released && s.down) || (s.pressed && s.released))
			return false;
		for (std::size_t j = 0; j < i; ++j) {
			if (s.name == states[j].name)
				return false;
		}
	}
	return true;
}
