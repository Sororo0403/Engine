#include "gameplay/GameActionReader.h"
#include "input/Gamepad.h"
#include "input/Keyboard.h"
#include "input/Mouse.h"
#include <algorithm>

namespace {
/// <summary>
/// 選んだトリガーやスティック方向の強さを取得する
/// </summary>
float AxisValue(GameActionBinding::GamepadAxis axis,
                const GamepadState &state) {
	using Axis = GameActionBinding::GamepadAxis;

	switch (axis) {
	case Axis::LEFT_TRIGGER:
		return state.leftTrigger;
	case Axis::RIGHT_TRIGGER:
		return state.rightTrigger;
	case Axis::LEFT_UP:
		return state.leftStick.y;
	case Axis::LEFT_DOWN:
		return -state.leftStick.y;
	case Axis::LEFT_LEFT:
		return -state.leftStick.x;
	case Axis::LEFT_RIGHT:
		return state.leftStick.x;
	case Axis::RIGHT_UP:
		return state.rightStick.y;
	case Axis::RIGHT_DOWN:
		return -state.rightStick.y;
	case Axis::RIGHT_LEFT:
		return -state.rightStick.x;
	case Axis::RIGHT_RIGHT:
		return state.rightStick.x;
	}

	return 0;
}
} // namespace

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
			const bool padConnected = gamepad && gamepad->IsConnected();
			const std::array<bool, 4> allowed{
			    true, pointerEnabled && mouse && mouse->IsConnected(),
			    padConnected, padConnected};
			// 押下と解放で閾値を分け、復帰時も解放側の閾値まで戻るのを待つ。
			constexpr float pressThreshold = .6f;
			constexpr float releaseThreshold = .4f;
			const auto strength =
			    binding.gamepadAxis && padConnected
			        ? AxisValue(*binding.gamepadAxis, gamepad->GetState())
			        : 0.f;
			const bool axisDown = h.held.back() || h.blocked.back()
			                          ? strength > releaseThreshold
			                          : strength >= pressThreshold;
			const std::array<bool, 4> raw{
			    binding.keyboard && keyboard.IsDown(*binding.keyboard),
			    binding.mouse && mouse && mouse->IsDown(*binding.mouse),
			    binding.gamepad && gamepad && gamepad->IsDown(*binding.gamepad),
			    axisDown};
			bool down = false;
			bool cancelled = false;
			float value = 0.f;

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

				if (allowed[i] && !h.blocked[i]) {
					const float strengthForDevice =
					    i == raw.size() - 1 ? (std::max)(0.f, strength)
					                        : (h.held[i] ? 1.f : 0.f);
					value = (std::max)(value, strengthForDevice);
				}
			}

			states_.push_back({binding.name, down, down && !h.down,
			                   !down && h.down && !cancelled, value});
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
