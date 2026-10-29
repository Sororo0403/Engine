#include "platform/windows/Keyboard.h"

void Keyboard::Update(HWND hwnd) {
	keys_.fill(false);

	// 自分のウィンドウを操作しているときだけ入力を受け付ける
	if (!hwnd || GetForegroundWindow() != hwnd) {
		return;
	}

	for (int key = 0; key < static_cast<int>(keys_.size()); ++key) {
		keys_[key] = (GetAsyncKeyState(key) & 0x8000) != 0;
	}
}

bool Keyboard::IsDown(int key) const {
	if (key < 0 || key >= static_cast<int>(keys_.size())) {
		return false;
	}

	return keys_[key];
}
