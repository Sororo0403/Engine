#include "gameplay/GameActionReader.h"
#include "input/Gamepad.h"
#include "input/Keyboard.h"
#include "input/Mouse.h"
#include <algorithm>

std::span<const GameActionState>
GameActionReader::Read(std::span<const GameActionBinding> bindings,
                       const Keyboard &keyboard, const Mouse *mouse,
                       const Gamepad *gamepad, bool enabled,
                       bool pointerEnabled) {
	if (!GameActionBinding::IsValid(bindings)) {
		Reset();
		return {};
	}
	std::vector<History> next;
	next.reserve(bindings.size());
	states_.clear();
	states_.reserve(bindings.size());
	for (const auto &binding : bindings) {
		const auto previous =
		    std::find_if(history_.begin(), history_.end(),
		                 [&](const auto &h) { return h.binding == binding; });
		History h = previous == history_.end() ? History{binding} : *previous;
		if (!enabled) {
			h.blocked.fill(true);
			h.held.fill(false);
			h.down = false;
			states_.push_back({binding.name});
		} else {
			const std::array<bool, 3> allowed{
			    true, pointerEnabled && mouse && mouse->IsConnected(),
			    gamepad && gamepad->IsConnected()};
			const std::array<bool, 3> raw{
			    binding.keyboard && keyboard.IsDown(*binding.keyboard),
			    binding.mouse && mouse && mouse->IsDown(*binding.mouse),
			    binding.gamepad && gamepad &&
			        gamepad->IsDown(*binding.gamepad)};
			bool down = false;
			bool cancelled = false;
			for (std::size_t i = 0; i < raw.size(); ++i) {
				if (!allowed[i]) {
					h.blocked[i] = true;
					cancelled = cancelled || h.held[i];
				} else if (!raw[i]) {
					h.blocked[i] = false;
				} else if (!h.blocked[i]) {
					down = true;
				}
				h.held[i] = allowed[i] && raw[i] && !h.blocked[i];
			}
			states_.push_back({binding.name, down, down && !h.down,
			                   !down && h.down && !cancelled});
			h.down = down;
		}
		next.push_back(std::move(h));
	}
	history_ = std::move(next);
	return states_;
}

void GameActionReader::Reset() {
	history_.clear();
	states_.clear();
}
