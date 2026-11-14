#pragma once

/// <summary>
/// 1台目のWiiリモコンの入力。shakeは振りを検出したフレームだけtrueになる。
/// </summary>
struct WiiRemoteState {
	bool connected = false;
	bool aDown = false;
	bool shake = false;
};
