#include "input/Mouse.h"

void Mouse::Update(const MouseState &state) {
	if (!state.connected) {
		Reset();
		return;
	}

	const bool wasConnected = state_.connected;

	previousButtons_ = state_.buttons;
	state_ = state;

	if (!wasConnected) {
		// 接続直後やフォーカス復帰時の押下を、
		// 新しいクリックとして扱わない
		previousButtons_ = state_.buttons;

		state_.deltaX = 0;
		state_.deltaY = 0;
		state_.wheelX = 0;
		state_.wheelY = 0;
	}
}

bool Mouse::IsConnected() const {
	return state_.connected;
}

bool Mouse::IsDown(MouseButton button) const {
	const auto index = static_cast<std::size_t>(button);

	if (!state_.connected || index >= state_.buttons.size()) {
		return false;
	}

	return state_.buttons[index];
}

bool Mouse::IsPressed(MouseButton button) const {
	const auto index = static_cast<std::size_t>(button);

	if (!state_.connected || index >= state_.buttons.size()) {
		return false;
	}

	return state_.buttons[index] && !previousButtons_[index];
}

bool Mouse::IsReleased(MouseButton button) const {
	const auto index = static_cast<std::size_t>(button);

	if (!state_.connected || index >= state_.buttons.size()) {
		return false;
	}

	return !state_.buttons[index] && previousButtons_[index];
}

const MouseState &Mouse::GetState() const {
	return state_;
}

void Mouse::Reset() {
	state_ = {};
	previousButtons_.fill(false);
}
