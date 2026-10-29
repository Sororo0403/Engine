#include "input/Gamepad.h"

#include <algorithm>
#include <cmath>

void Gamepad::Update(const GamepadState &state) {
	if (!state.connected) {
		Reset();
		return;
	}

	previousState_ = state_;
	state_ = state;

	state_.leftStick = ApplyDeadZone(state.leftStick);
	state_.rightStick = ApplyDeadZone(state.rightStick);

	state_.leftTrigger = std::clamp(state.leftTrigger, 0.0f, 1.0f);
	state_.rightTrigger = std::clamp(state.rightTrigger, 0.0f, 1.0f);
}

bool Gamepad::IsConnected() const { return state_.connected; }

bool Gamepad::IsDown(GamepadButton button) const {
	const auto index = static_cast<std::size_t>(button);

	if (!state_.connected || index >= state_.buttons.size()) {
		return false;
	}

	return state_.buttons[index];
}

bool Gamepad::IsPressed(GamepadButton button) const {
	const auto index = static_cast<std::size_t>(button);

	if (!state_.connected || index >= state_.buttons.size()) {
		return false;
	}

	return state_.buttons[index] && !previousState_.buttons[index];
}

bool Gamepad::IsReleased(GamepadButton button) const {
	const auto index = static_cast<std::size_t>(button);

	if (!state_.connected || index >= state_.buttons.size()) {
		return false;
	}

	return !state_.buttons[index] && previousState_.buttons[index];
}

const GamepadState &Gamepad::GetState() const { return state_; }

void Gamepad::Reset() {
	state_ = {};
	previousState_ = {};
}

GamepadStick Gamepad::ApplyDeadZone(GamepadStick stick) {
	const float magnitude = std::sqrt(stick.x * stick.x + stick.y * stick.y);

	if (magnitude <= kStickDeadZone) {
		return {};
	}

	// デッドゾーンの外側を0～1に変換する
	const float strength =
	    (std::min(magnitude, 1.0f) - kStickDeadZone) / (1.0f - kStickDeadZone);

	// 倒した方向を保ちながら、入力の強さを適用する
	return {stick.x / magnitude * strength, stick.y / magnitude * strength};
}
