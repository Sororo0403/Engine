#include "input/Keyboard.h"

void Keyboard::Update(const State &states) {
	previousKeys_ = keys_;
	keys_ = states;
}

bool Keyboard::IsDown(Key key) const {
	const auto index = static_cast<std::size_t>(key);

	if (key == Key::UNKNOWN || index >= keys_.size()) {
		return false;
	}

	return keys_[index];
}

bool Keyboard::IsPressed(Key key) const {
	const auto index = static_cast<std::size_t>(key);

	if (key == Key::UNKNOWN || index >= keys_.size()) {
		return false;
	}

	return keys_[index] && !previousKeys_[index];
}

bool Keyboard::IsReleased(Key key) const {
	const auto index = static_cast<std::size_t>(key);

	if (key == Key::UNKNOWN || index >= keys_.size()) {
		return false;
	}

	return !keys_[index] && previousKeys_[index];
}

void Keyboard::Reset() {
	keys_.fill(false);
	previousKeys_.fill(false);
}
