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

	if (!ReadKeyboard(state.keyboard)) {
		return false;
	}

	if (!ReadGamepad(state.gamepad)) {
		return false;
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

bool WindowsInputBackend::ReadKeyboard(Keyboard::State &states) {
	states.fill(false);

	Microsoft::WRL::ComPtr<GameInput::v4::IGameInputReading> reading;

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
				states[static_cast<std::size_t>(key)] = true;
			}
		}
	}

	return true;
}

bool WindowsInputBackend::ReadGamepad(GamepadState &state) {
	using namespace GameInput::v4;

	state = {};

	Microsoft::WRL::ComPtr<IGameInputReading> reading;

	const HRESULT hr = gameInput_->GetCurrentReading(
	    GameInputKindGamepad, nullptr, reading.GetAddressOf());

	if (hr == GAMEINPUT_E_READING_NOT_FOUND ||
	    hr == GAMEINPUT_E_DEVICE_NOT_FOUND ||
	    hr == GAMEINPUT_E_DEVICE_DISCONNECTED) {
		return true;
	}

	if (FAILED(hr)) {
		Logger::Error(std::format(
		    "コントローラー状態の取得に失敗しました。HRESULT=0x{:08X}",
		    static_cast<unsigned long>(hr)));
		return false;
	}

	GameInputGamepadState nativeState{};

	if (!reading->GetGamepadState(&nativeState)) {
		Logger::Error("コントローラー状態の読み取りに失敗しました。");
		return false;
	}

	// 共通ボタンとGameInputのボタンの対応表
	struct ButtonMapping {
		GamepadButton button;
		GameInputGamepadButtons flag;
	};

	static constexpr ButtonMapping mappings[] = {
	    {GamepadButton::SOUTH, GameInputGamepadA},
	    {GamepadButton::EAST, GameInputGamepadB},
	    {GamepadButton::WEST, GameInputGamepadX},
	    {GamepadButton::NORTH, GameInputGamepadY},

	    {GamepadButton::DPAD_UP, GameInputGamepadDPadUp},
	    {GamepadButton::DPAD_DOWN, GameInputGamepadDPadDown},
	    {GamepadButton::DPAD_LEFT, GameInputGamepadDPadLeft},
	    {GamepadButton::DPAD_RIGHT, GameInputGamepadDPadRight},

	    {GamepadButton::LEFT_SHOULDER, GameInputGamepadLeftShoulder},
	    {GamepadButton::RIGHT_SHOULDER, GameInputGamepadRightShoulder},

	    {GamepadButton::LEFT_STICK, GameInputGamepadLeftThumbstick},
	    {GamepadButton::RIGHT_STICK, GameInputGamepadRightThumbstick},

	    {GamepadButton::MENU, GameInputGamepadMenu},
	    {GamepadButton::VIEW, GameInputGamepadView},
	};

	for (const auto &mapping : mappings) {
		const auto index = static_cast<std::size_t>(mapping.button);

		state.buttons[index] =
		    (nativeState.buttons & mapping.flag) != GameInputGamepadNone;
	}

	state.leftStick = {nativeState.leftThumbstickX,
	                   nativeState.leftThumbstickY};

	state.rightStick = {nativeState.rightThumbstickX,
	                    nativeState.rightThumbstickY};

	state.leftTrigger = nativeState.leftTrigger;
	state.rightTrigger = nativeState.rightTrigger;
	state.connected = true;

	return true;
}
