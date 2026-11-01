#include "input/InputSystem.h"
#include "core/Logger.h"

#include <utility>

InputSystem::~InputSystem() { Finalize(); }

bool InputSystem::Initialize(std::unique_ptr<IInputBackend> backend) {
	if (backend_) {
		Logger::Error("InputSystem::Initialize: 初期化済みです。");
		return false;
	}

	if (!backend) {
		Logger::Error("入力バックエンドが指定されていません。");
		return false;
	}

	if (!backend->Initialize()) {
		backend->Finalize();
		return false;
	}

	backend_ = std::move(backend);

	keyboard_.Reset();
	mouse_.Reset();
	gamepad_.Reset();

	return true;
}

bool InputSystem::Update() {
	active_ = false;
	if (!backend_) {
		Logger::Error("InputSystem::Update: 初期化されていません。");
		return false;
	}

	InputState state{};

	if (!backend_->Update(state)) {
		keyboard_.Reset();
		mouse_.Reset();
		gamepad_.Reset();
		return false;
	}

	if (!state.active) {
		keyboard_.Reset();
		mouse_.Reset();
		gamepad_.Reset();
		return true;
	}

	active_ = true;
	keyboard_.Update(state.keyboard);
	mouse_.Update(state.mouse);
	gamepad_.Update(state.gamepad);

	return true;
}

void InputSystem::Finalize() {
	active_ = false;
	keyboard_.Reset();
	mouse_.Reset();
	gamepad_.Reset();

	if (backend_) {
		backend_->Finalize();
		backend_.reset();
	}
}

const Keyboard &InputSystem::GetKeyboard() const { return keyboard_; }

const Mouse &InputSystem::GetMouse() const { return mouse_; }

const Gamepad &InputSystem::GetGamepad() const { return gamepad_; }
