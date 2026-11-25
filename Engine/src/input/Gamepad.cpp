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

	rawLeft_ = state.leftStick;
	rawRight_ = state.rightStick;
	state_.leftStick = ApplyDeadZone(rawLeft_, kStickDeadZone);
	state_.rightStick = ApplyDeadZone(rawRight_, kStickDeadZone);

	state_.leftTrigger = std::isfinite(state.leftTrigger)
	                         ? std::clamp(state.leftTrigger, 0.0f, 1.0f)
	                         : 0.0f;
	state_.rightTrigger = std::isfinite(state.rightTrigger)
	                          ? std::clamp(state.rightTrigger, 0.0f, 1.0f)
	                          : 0.0f;
}

bool Gamepad::IsConnected() const {
	return state_.connected;
}

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

const GamepadState &Gamepad::GetState() const {
	return state_;
}

Gamepad Gamepad::WithDeadZones(float left, float right) const {
	auto result = *this;
	if (state_.connected) {
		result.state_.leftStick = ApplyDeadZone(rawLeft_, left);
		result.state_.rightStick = ApplyDeadZone(rawRight_, right);
	}
	return result;
}

void Gamepad::Reset() {
	state_ = {};
	previousState_ = {};
	rawLeft_ = {};
	rawRight_ = {};
}

GamepadStick Gamepad::ApplyDeadZone(GamepadStick stick, float deadZone) {
	if (!std::isfinite(stick.x) || !std::isfinite(stick.y)) {
		return {};
	}

	const double magnitude = std::hypot(double(stick.x), double(stick.y));
	if (!(deadZone >= 0 && deadZone < 1))
		deadZone = kStickDeadZone;

	if (magnitude <= deadZone) {
		return {};
	}

	// デッドゾーンの外側を0～1に変換する
	const double strength =
	    (std::min(magnitude, 1.0) - deadZone) / (1.0 - deadZone);

	// 倒した方向を保ちながら、入力の強さを適用する
	return {float(stick.x / magnitude * strength),
	        float(stick.y / magnitude * strength)};
}
