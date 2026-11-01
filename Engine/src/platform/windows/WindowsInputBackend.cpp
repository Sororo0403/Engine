#include "platform/windows/WindowsInputBackend.h"
#include "core/Logger.h"

#include <format>
#include <vector>

WindowsInputBackend::WindowsInputBackend(HWND hwnd) : hwnd_(hwnd) {}

WindowsInputBackend::~WindowsInputBackend() { Finalize(); }

bool WindowsInputBackend::Initialize() {
	using namespace GameInput::v4;

	if (gameInput_) {
		Logger::Error("WindowsInputBackend::Initialize: 初期化済みです。");
		return false;
	}

	if (!IsWindow(hwnd_)) {
		Logger::Error("入力対象のウィンドウが無効です。");
		return false;
	}

	HRESULT hr = GameInputCreate(gameInput_.GetAddressOf());

	if (FAILED(hr)) {
		Logger::Error(
		    std::format("GameInputの初期化に失敗しました。HRESULT=0x{:08X}",
		                static_cast<unsigned long>(hr)));
		return false;
	}

	hr = gameInput_->CreateAggregateDevice(GameInputKindKeyboard,
	                                       &keyboardDeviceId_);

	if (FAILED(hr)) {
		Logger::Error(std::format("キーボードの集約デバイス作成に失敗しました。"
		                          "HRESULT=0x{:08X}",
		                          static_cast<unsigned long>(hr)));

		Finalize();
		return false;
	}

	keyboardAggregateCreated_ = true;
	keyboardHadReading_ = false;

	hr = gameInput_->CreateAggregateDevice(GameInputKindMouse,
	                                       &mouseDeviceId_);

	if (FAILED(hr)) {
		Logger::Error(std::format("マウスの集約デバイス作成に失敗しました。"
		                          "HRESULT=0x{:08X}",
		                          static_cast<unsigned long>(hr)));
		Finalize();
		return false;
	}

	mouseAggregateCreated_ = true;
	mouseHadReading_ = false;

	// デバイスのインターフェース取得は、入力更新時に行う
	Logger::Info("GameInputとキーボード・マウスの集約機能を初期化しました。");
	return true;
}

bool WindowsInputBackend::Update(InputState &state) {
	state = {};

	if (!gameInput_) {
		Logger::Error("WindowsInputBackend::Update: 初期化されていません。");
		return false;
	}

	if (GetForegroundWindow() != hwnd_) {
		// 復帰時に非アクティブ中の移動量を反映しない
		ResetMouseReading();
		return true;
	}

	if (!ReadKeyboard(state.keyboard) || !ReadMouse(state.mouse) ||
	    !ReadGamepad(state.gamepad)) {
		ResetMouseReading();
		return false;
	}

	state.active = true;
	return true;
}

void WindowsInputBackend::Finalize() {
	ResetMouseReading();

	keyboardDevice_.Reset();
	keyboardHadReading_ = false;

	if (gameInput_ && mouseAggregateCreated_) {
		const HRESULT hr =
		    gameInput_->DisableAggregateDevice(&mouseDeviceId_);

		if (FAILED(hr)) {
			Logger::Warning(std::format("マウスの集約解除に失敗しました。"
			                            "HRESULT=0x{:08X}",
			                            static_cast<unsigned long>(hr)));
		}
	}

	mouseAggregateCreated_ = false;
	mouseDeviceId_ = {};

	if (gameInput_ && keyboardAggregateCreated_) {
		const HRESULT hr =
		    gameInput_->DisableAggregateDevice(&keyboardDeviceId_);

		if (FAILED(hr)) {
			Logger::Warning(std::format("キーボードの集約解除に失敗しました。"
			                            "HRESULT=0x{:08X}",
			                            static_cast<unsigned long>(hr)));
		}
	}

	keyboardAggregateCreated_ = false;
	keyboardDeviceId_ = {};

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
	case VK_F2:
		return Key::F2;
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
	using namespace GameInput::v4;

	states.fill(false);

	if (!gameInput_ || !keyboardAggregateCreated_) {
		Logger::Error("WindowsInputBackend::ReadKeyboard: 未初期化です。");
		return false;
	}

	if (!keyboardDevice_) {
		const HRESULT hr = gameInput_->FindDeviceFromId(
		    &keyboardDeviceId_, keyboardDevice_.GetAddressOf());

		// 作成直後など、まだ取得できない場合は次フレームに再試行する
		if (hr == GAMEINPUT_E_DEVICE_NOT_FOUND ||
		    hr == GAMEINPUT_E_DEVICE_DISCONNECTED) {
			return true;
		}

		if (FAILED(hr)) {
			Logger::Error(std::format("集約キーボードの取得に失敗しました。"
			                          "HRESULT=0x{:08X}",
			                          static_cast<unsigned long>(hr)));
			return false;
		}

		if (!keyboardDevice_) {
			return true;
		}
	}

	Microsoft::WRL::ComPtr<IGameInputReading> reading;

	const HRESULT hr = gameInput_->GetCurrentReading(
	    GameInputKindKeyboard, keyboardDevice_.Get(), reading.GetAddressOf());

	if (hr == GAMEINPUT_E_READING_NOT_FOUND ||
	    hr == GAMEINPUT_E_DEVICE_NOT_FOUND ||
	    hr == GAMEINPUT_E_DEVICE_DISCONNECTED) {
		// 毎フレーム出力せず、取得できていた状態から途切れた時だけ記録する
		if (keyboardHadReading_) {
			Logger::Warning(std::format("キーボードの入力取得が途切れました。"
			                            "HRESULT=0x{:08X}",
			                            static_cast<unsigned long>(hr)));
		}

		keyboardHadReading_ = false;

		if (hr != GAMEINPUT_E_READING_NOT_FOUND) {
			keyboardDevice_.Reset();
		}

		return true;
	}

	if (FAILED(hr)) {
		Logger::Error(
		    std::format("キーボード状態の取得に失敗しました。HRESULT=0x{:08X}",
		                static_cast<unsigned long>(hr)));
		return false;
	}

	if (!keyboardHadReading_) {
		Logger::Info("集約キーボードの入力取得を開始しました。");
	}

	keyboardHadReading_ = true;

	const uint32_t keyCount = reading->GetKeyCount();

	if (keyCount == 0) {
		return true;
	}

	std::vector<GameInputKeyState> keyStates(keyCount);

	const uint32_t count = reading->GetKeyState(keyCount, keyStates.data());

	for (uint32_t i = 0; i < count; ++i) {
		const Key key = ToKey(keyStates[i].virtualKey);

		if (key != Key::UNKNOWN) {
			states[static_cast<std::size_t>(key)] = true;
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

bool WindowsInputBackend::ReadMouse(MouseState &state) {
	using namespace GameInput::v4;

	state = {};

	if (!gameInput_ || !mouseAggregateCreated_) {
		Logger::Error("WindowsInputBackend::ReadMouse: 未初期化です。");
		return false;
	}

	if (!mouseDevice_) {
		const HRESULT hr = gameInput_->FindDeviceFromId(
		    &mouseDeviceId_, mouseDevice_.GetAddressOf());

		// 集約デバイスの準備が完了するまで、次フレームに再試行する
		if (hr == GAMEINPUT_E_DEVICE_NOT_FOUND ||
		    hr == GAMEINPUT_E_DEVICE_DISCONNECTED) {
			return true;
		}

		if (FAILED(hr)) {
			Logger::Error(std::format("集約マウスの取得に失敗しました。"
			                          "HRESULT=0x{:08X}",
			                          static_cast<unsigned long>(hr)));
			return false;
		}

		if (!mouseDevice_) {
			return true;
		}
	}

	Microsoft::WRL::ComPtr<IGameInputReading> reading;

	const HRESULT hr = gameInput_->GetCurrentReading(
	    GameInputKindMouse, mouseDevice_.Get(), reading.GetAddressOf());

	if (hr == GAMEINPUT_E_READING_NOT_FOUND ||
	    hr == GAMEINPUT_E_DEVICE_NOT_FOUND ||
	    hr == GAMEINPUT_E_DEVICE_DISCONNECTED) {
		if (mouseHadReading_) {
			Logger::Warning(std::format("マウスの入力取得が途切れました。"
			                            "HRESULT=0x{:08X}",
			                            static_cast<unsigned long>(hr)));
		}

		ResetMouseReading();
		return true;
	}

	if (FAILED(hr)) {
		ResetMouseReading();

		Logger::Error(
		    std::format("マウス状態の取得に失敗しました。HRESULT=0x{:08X}",
		                static_cast<unsigned long>(hr)));
		return false;
	}

	GameInputMouseState nativeState{};

	if (!reading->GetMouseState(&nativeState)) {
		ResetMouseReading();
		Logger::Error("マウス状態の読み取りに失敗しました。");
		return false;
	}

	if (!mouseHadReading_) {
		Logger::Info("集約マウスの入力取得を開始しました。");
	}

	mouseHadReading_ = true;

	struct ButtonMapping {
		MouseButton button;
		GameInputMouseButtons flag;
	};

	static constexpr ButtonMapping mappings[] = {
	    {MouseButton::LEFT, GameInputMouseLeftButton},
	    {MouseButton::RIGHT, GameInputMouseRightButton},
	    {MouseButton::MIDDLE, GameInputMouseMiddleButton},
	    {MouseButton::BUTTON4, GameInputMouseButton4},
	    {MouseButton::BUTTON5, GameInputMouseButton5},
	};

	for (const auto &mapping : mappings) {
		const auto index = static_cast<std::size_t>(mapping.button);

		state.buttons[index] =
		    (nativeState.buttons & mapping.flag) != GameInputMouseNone;
	}

	// GameInputの累積値から、前回取得時との差分を求める
	if (mouseReadingValid_) {
		state.deltaX = nativeState.positionX - previousMouseState_.positionX;
		state.deltaY = nativeState.positionY - previousMouseState_.positionY;

		state.wheelX = nativeState.wheelX - previousMouseState_.wheelX;
		state.wheelY = nativeState.wheelY - previousMouseState_.wheelY;
	}

	previousMouseState_ = nativeState;
	mouseReadingValid_ = true;

	// UIなどで使うカーソル座標は、相対移動量とは別に取得する
	POINT position{};

	if (GetCursorPos(&position) && ScreenToClient(hwnd_, &position)) {
		state.positionValid = true;
		state.positionX = position.x;
		state.positionY = position.y;
	}

	state.connected = true;
	return true;
}

void WindowsInputBackend::ResetMouseReading() {
	mouseDevice_.Reset();
	previousMouseState_ = {};
	mouseReadingValid_ = false;
	mouseHadReading_ = false;
}
