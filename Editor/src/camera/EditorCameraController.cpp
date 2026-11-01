#include "camera/EditorCameraController.h"

#include "graphics/ui/ViewportInput.h"
#include "input/InputSystem.h"
#include "input/Key.h"

#include <algorithm>
#include <cmath>

void EditorCameraController::Update(const InputSystem &input,
                                    const ViewportInput &viewport,
                                    float deltaTime) {
	if (!std::isfinite(deltaTime) || deltaTime < 0.0f) {
		navigating_ = false;
		return;
	}

	const Keyboard &keyboard = input.GetKeyboard();
	const Mouse &mouse = input.GetMouse();

	// 他のUI操作やフォーカス喪失を優先する
	if (!viewport.applicationFocused || !viewport.windowFocused ||
	    viewport.widgetActive || !mouse.IsConnected() || !viewport.rightDown ||
	    keyboard.IsPressed(Key::ESCAPE)) {
		navigating_ = false;
		return;
	}

	bool started = false;

	if (!navigating_) {
		// Scene画像以外で押した右ボタンでは操作を開始しない
		if (!viewport.imageHovered || !viewport.rightPressed) {
			return;
		}

		navigating_ = true;
		started = true;
	}

	// 操作開始前の移動量を回転へ反映しない
	if (!started) {
		const MouseState &mouseState = mouse.GetState();

		// 相対移動量にはdeltaTimeを掛けない
		yaw_ += static_cast<float>(mouseState.deltaX) * mouseSensitivity_;

		pitch_ -= static_cast<float>(mouseState.deltaY) * mouseSensitivity_;

		yaw_ = std::remainder(yaw_, DirectX::XM_2PI);

		constexpr float kPitchLimit = DirectX::XM_PIDIV2 - 0.01f;

		// 視線と上方向が平行にならないよう制限する
		pitch_ = std::clamp(pitch_, -kPitchLimit, kPitchLimit);
	}

	float moveRight = 0.0f;
	float moveUp = 0.0f;
	float moveForward = 0.0f;

	if (keyboard.IsDown(Key::D)) {
		moveRight += 1.0f;
	}

	if (keyboard.IsDown(Key::A)) {
		moveRight -= 1.0f;
	}

	if (keyboard.IsDown(Key::E)) {
		moveUp += 1.0f;
	}

	if (keyboard.IsDown(Key::Q)) {
		moveUp -= 1.0f;
	}

	if (keyboard.IsDown(Key::W)) {
		moveForward += 1.0f;
	}

	if (keyboard.IsDown(Key::S)) {
		moveForward -= 1.0f;
	}

	const auto forward = GetForward();

	const DirectX::XMFLOAT3 right{
	    std::cos(yaw_),
	    0.0f,
	    -std::sin(yaw_),
	};

	DirectX::XMFLOAT3 movement{
	    right.x * moveRight + forward.x * moveForward,
	    moveUp + forward.y * moveForward,
	    right.z * moveRight + forward.z * moveForward,
	};

	const float lengthSquared = movement.x * movement.x +
	                            movement.y * movement.y +
	                            movement.z * movement.z;

	if (lengthSquared <= 0.000001f) {
		return;
	}

	const float multiplier =
	    keyboard.IsDown(Key::SHIFT) ? fastMultiplier_ : 1.0f;

	// 処理落ちや停止からの復帰で大きく移動しないよう制限する
	const float time = std::min(deltaTime, 0.1f);

	// 斜め方向でも移動速度が増えないよう正規化する
	const float distance =
	    moveSpeed_ * multiplier * time / std::sqrt(lengthSquared);

	position_.x += movement.x * distance;
	position_.y += movement.y * distance;
	position_.z += movement.z * distance;
}

DirectX::XMFLOAT3 EditorCameraController::GetForward() const {
	const float cosPitch = std::cos(pitch_);

	return {
	    std::sin(yaw_) * cosPitch,
	    std::sin(pitch_),
	    std::cos(yaw_) * cosPitch,
	};
}

CameraPose EditorCameraController::GetPose() const {
	const auto forward = GetForward();

	CameraPose pose;
	pose.position = position_;

	pose.target = {
	    position_.x + forward.x,
	    position_.y + forward.y,
	    position_.z + forward.z,
	};

	return pose;
}

void EditorCameraController::Reset() {
	position_ = {0.0f, 0.0f, -2.0f};
	yaw_ = 0.0f;
	pitch_ = 0.0f;
	navigating_ = false;
}
