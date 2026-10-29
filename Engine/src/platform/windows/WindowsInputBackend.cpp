#include "platform/windows/WindowsInputBackend.h"
#include "core/Logger.h"

#include <format>
#include <vector>

WindowsInputBackend::WindowsInputBackend(HWND hwnd) : hwnd_(hwnd) {}

WindowsInputBackend::~WindowsInputBackend() { Finalize(); }

bool WindowsInputBackend::Initialize() {
	if (gameInput_) {
		Logger::Error("WindowsInputBackend::Initialize: 初期化済みです。");
		return false;
	}

	if (!IsWindow(hwnd_)) {
		Logger::Error("入力対象のウィンドウが無効です。");
		return false;
	}

	const HRESULT hr =
	    GameInput::v4::GameInputCreate(gameInput_.GetAddressOf());

	if (FAILED(hr)) {
		Logger::Error(
		    std::format("GameInputの初期化に失敗しました。HRESULT=0x{:08X}",
		                static_cast<unsigned long>(hr)));
		return false;
	}

	Logger::Info("GameInputを初期化しました。");
	return true;
}

bool WindowsInputBackend::Update(InputState &state) {
	state = {};

	if (!gameInput_) {
		Logger::Error("WindowsInputBackend::Update: 初期化されていません。");
		return false;
	}

	if (GetForegroundWindow() != hwnd_) {
		return true;
	}

	Microsoft::WRL::ComPtr<GameInput::v4::IGameInputReading> reading;

	// 最新のキーボード状態を取得する
	const HRESULT hr = gameInput_->GetCurrentReading(
	    GameInput::v4::GameInputKindKeyboard, nullptr, reading.GetAddressOf());

	if (hr == GameInput::v4::GAMEINPUT_E_READING_NOT_FOUND ||
	    hr == GameInput::v4::GAMEINPUT_E_DEVICE_NOT_FOUND ||
	    hr == GameInput::v4::GAMEINPUT_E_DEVICE_DISCONNECTED) {
		return true;
	}

	if (FAILED(hr)) {
		Logger::Error(
		    std::format("キーボード状態の取得に失敗しました。HRESULT=0x{:08X}",
		                static_cast<unsigned long>(hr)));
		return false;
	}

	const uint32_t keyCount = reading->GetKeyCount();

	if (keyCount > 0) {
		std::vector<GameInput::v4::GameInputKeyState> keyStates(keyCount);

		const uint32_t count = reading->GetKeyState(keyCount, keyStates.data());

		for (uint32_t i = 0; i < count; ++i) {
			const Key key = ToKey(keyStates[i].virtualKey);

			if (key != Key::UNKNOWN) {
				state.keyboard[static_cast<std::size_t>(key)] = true;
			}
		}
	}

	state.active = true;
	return true;
}

void WindowsInputBackend::Finalize() {
	if (gameInput_) {
		gameInput_.Reset();
		Logger::Info("GameInputを終了しました。");
	}
}

Key WindowsInputBackend::ToKey(unsigned int virtualKey) {
	if (virtualKey >= 'A' && virtualKey <= 'Z') {
		return static_cast<Key>(static_cast<std::size_t>(Key::A) + virtualKey -
		                        'A');
	}

	if (virtualKey >= '0' && virtualKey <= '9') {
		return static_cast<Key>(static_cast<std::size_t>(Key::DIGIT_0) +
		                        virtualKey - '0');
	}

	switch (virtualKey) {
	case VK_SPACE:
		return Key::SPACE;
	case VK_RETURN:
		return Key::ENTER;
	case VK_ESCAPE:
		return Key::ESCAPE;
	case VK_TAB:
		return Key::TAB;
	case VK_BACK:
		return Key::BACKSPACE;
	case VK_DELETE:
		return Key::DELETE_KEY;
	case VK_LEFT:
		return Key::LEFT;
	case VK_RIGHT:
		return Key::RIGHT;
	case VK_UP:
		return Key::UP;
	case VK_DOWN:
		return Key::DOWN;
	case VK_SHIFT:
	case VK_LSHIFT:
	case VK_RSHIFT:
		return Key::SHIFT;
	case VK_CONTROL:
	case VK_LCONTROL:
	case VK_RCONTROL:
		return Key::CTRL;
	case VK_MENU:
	case VK_LMENU:
	case VK_RMENU:
		return Key::ALT;
	default:
		return Key::UNKNOWN;
	}
}
