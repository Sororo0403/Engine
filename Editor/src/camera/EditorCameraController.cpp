#include "camera/EditorCameraController.h"

#include "graphics/ui/ViewportInput.h"
#include "input/InputSystem.h"
#include "input/Key.h"
#include "math/MathUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>

bool EditorCameraController::SetSettings(const Settings &settings) {
	if (navigating_ || panning_ || orbiting_ || !std::isfinite(settings.moveSpeed) ||
	    settings.moveSpeed <= 0.0f || settings.moveSpeed > kMaximumMoveSpeed ||
	    !std::isfinite(settings.fastMultiplier) || settings.fastMultiplier < 1.0f ||
	    settings.fastMultiplier > kMaximumFastMultiplier ||
	    !std::isfinite(settings.mouseSensitivity) || settings.mouseSensitivity <= 0.0f ||
	    settings.mouseSensitivity > kMaximumMouseSensitivity ||
	    !std::isfinite(settings.wheelDistance) || settings.wheelDistance <= 0.0f ||
	    settings.wheelDistance > kMaximumWheelDistance ||
	    !std::isfinite(settings.panSensitivity) || settings.panSensitivity <= 0.0f ||
	    settings.panSensitivity > kMaximumPanSensitivity) {
		return false;
	}
	settings_ = settings;
	return true;
}

EditorCameraController::Settings EditorCameraController::GetSettings() const {
	return settings_;
}

void EditorCameraController::Update(const InputSystem &input,
                                    const ViewportInput &viewport,
                                    float deltaTime, const DirectX::XMFLOAT3 *orbitTarget) {
	if (!std::isfinite(deltaTime) || deltaTime < 0.0f) {
		navigating_ = false;
		panning_ = false;
		orbiting_ = false;
		return;
	}

	const Keyboard &keyboard = input.GetKeyboard();
	const Mouse &mouse = input.GetMouse();

	// 他のUI操作やフォーカス喪失を優先する
	if (!viewport.applicationFocused || !viewport.windowFocused ||
	    viewport.widgetActive || !mouse.IsConnected() ||
	    keyboard.IsPressed(Key::ESCAPE)) {
		navigating_ = false;
		panning_ = false;
		orbiting_ = false;
		return;
	}

	if (!viewport.rightDown && !viewport.middleDown && viewport.leftDown &&
	    keyboard.IsDown(Key::ALT)) {
		navigating_ = false;
		panning_ = false;
		if (!orbiting_) {
			if (!viewport.leftPressed || !viewport.imageHovered || !orbitTarget ||
			    !MathUtils::IsFinite(*orbitTarget)) {
				return;
			}
			const double x = static_cast<double>(position_.x) - orbitTarget->x;
			const double y = static_cast<double>(position_.y) - orbitTarget->y;
			const double z = static_cast<double>(position_.z) - orbitTarget->z;
			if (x * x + y * y + z * z <= kMinimumPoseLengthSquared ||
			    std::abs(x) > (std::numeric_limits<float>::max)() ||
			    std::abs(y) > (std::numeric_limits<float>::max)() ||
			    std::abs(z) > (std::numeric_limits<float>::max)()) {
				return;
			}
			orbitCenter_ = *orbitTarget;
			orbiting_ = true;
			// 押す前の移動量は使わず、開始時のカメラの向きも保持する
			return;
		}
		const auto &state = mouse.GetState();
		const float yawDelta = std::remainder(
		    static_cast<float>(state.deltaX) * settings_.mouseSensitivity, DirectX::XM_2PI);
		const float nextYaw = std::remainder(yaw_ + yawDelta, DirectX::XM_2PI);
		const float nextPitch = std::clamp(
		    pitch_ - static_cast<float>(state.deltaY) * settings_.mouseSensitivity,
		    -kPitchLimit, kPitchLimit);
		using namespace DirectX;
		const XMFLOAT3 right{std::cos(nextYaw), 0.0f, -std::sin(nextYaw)};
		const auto rotation = XMMatrixRotationY(yawDelta) *
		                      XMMatrixRotationAxis(XMLoadFloat3(&right), -(nextPitch - pitch_));
		// 位置と視線に同じ回転を掛け、注視対象の画面上の位置を保つ
		const auto offset = XMVectorSubtract(XMLoadFloat3(&position_), XMLoadFloat3(&orbitCenter_));
		XMFLOAT3 next;
		XMStoreFloat3(&next, XMVectorAdd(XMLoadFloat3(&orbitCenter_),
		                               XMVector3TransformNormal(offset, rotation)));
		if (MathUtils::IsFinite(next)) {
			position_ = next;
			yaw_ = nextYaw;
			pitch_ = nextPitch;
		}
		return;
	}
	orbiting_ = false;

	if (!viewport.rightDown) {
		navigating_ = false;
		if (viewport.middleDown) {
			const bool started = !panning_;
			if (started && (!viewport.imageHovered || !viewport.middlePressed)) {
				return;
			}
			panning_ = true;
			if (!started) {
				const auto forward = GetForward();
				const DirectX::XMFLOAT3 right{std::cos(yaw_), 0.0f, -std::sin(yaw_)};
				DirectX::XMFLOAT3 up;
				DirectX::XMStoreFloat3(&up, DirectX::XMVector3Cross(
				    DirectX::XMLoadFloat3(&forward), DirectX::XMLoadFloat3(&right)));
				const auto &state = mouse.GetState();
				// ドラッグした方向に景色が動く。相対移動量には時間を掛けない
				const float x = -static_cast<float>(state.deltaX) * settings_.panSensitivity;
				const float y = static_cast<float>(state.deltaY) * settings_.panSensitivity;
				const DirectX::XMFLOAT3 next{position_.x + right.x * x + up.x * y,
				                             position_.y + right.y * x + up.y * y,
				                             position_.z + right.z * x + up.z * y};
				if (MathUtils::IsFinite(next)) {
					position_ = next;
				}
			}
			return;
		}
		panning_ = false;
		if (viewport.imageHovered && std::isfinite(viewport.wheelDelta) &&
		    viewport.wheelDelta != 0.0f) {
			// ホイールはフレーム内の変化量なので、deltaTimeを掛けない
			const float distance =
			    std::clamp(viewport.wheelDelta, -kMaximumWheelDelta, kMaximumWheelDelta) *
			    settings_.wheelDistance;
			const auto forward = GetForward();
			const DirectX::XMFLOAT3 next{position_.x + forward.x * distance,
			                             position_.y + forward.y * distance,
			                             position_.z + forward.z * distance};
			if (MathUtils::IsFinite(next)) {
				position_ = next;
			}
		}
		return;
	}

	panning_ = false;
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
		yaw_ += static_cast<float>(mouseState.deltaX) * settings_.mouseSensitivity;

		pitch_ -= static_cast<float>(mouseState.deltaY) * settings_.mouseSensitivity;

		yaw_ = std::remainder(yaw_, DirectX::XM_2PI);

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
	    keyboard.IsDown(Key::SHIFT) ? settings_.fastMultiplier : 1.0f;

	// 処理落ちや停止からの復帰で大きく移動しないよう制限する
	const float time = std::min(deltaTime, 0.1f);

	// 斜め方向でも移動速度が増えないよう正規化する
	const float distance =
	    settings_.moveSpeed * multiplier * time / std::sqrt(lengthSquared);

	position_.x += movement.x * distance;
	position_.y += movement.y * distance;
	position_.z += movement.z * distance;
}

bool EditorCameraController::FrameBounds(
    const std::array<DirectX::XMFLOAT3, 8> &corners,
    const DirectX::XMFLOAT4X4 &projection) {
	const double horizontalScale = projection._11;
	const double verticalScale = projection._22;
	const double depthScale = projection._33;
	const double depthOffset = projection._43;

	if (!std::isfinite(horizontalScale) || !std::isfinite(verticalScale) ||
	    !std::isfinite(depthScale) || !std::isfinite(depthOffset) ||
	    horizontalScale <= 0.0 || verticalScale <= 0.0 || depthScale <= 1.0 ||
	    depthOffset >= 0.0) {
		return false;
	}

	// 現在のCameraが使用する、中央対称・通常深度の透視投影を対象にする
	if (projection._34 != 1.0f || projection._44 != 0.0f ||
	    projection._31 != 0.0f || projection._32 != 0.0f) {
		return false;
	}

	const double nearClip = -depthOffset / depthScale;
	const double farClip = -depthOffset / (depthScale - 1.0);

	if (!std::isfinite(nearClip) || !std::isfinite(farClip) ||
	    nearClip <= 0.0 || farClip <= nearClip) {
		return false;
	}

	// ワールド座標の8頂点から、ボックスの中心を求める
	double centerX = 0.0;
	double centerY = 0.0;
	double centerZ = 0.0;

	const double inverseCount = 1.0 / static_cast<double>(corners.size());

	for (const auto &corner : corners) {
		if (!MathUtils::IsFinite(corner)) {
			return false;
		}

		centerX += static_cast<double>(corner.x) * inverseCount;
		centerY += static_cast<double>(corner.y) * inverseCount;
		centerZ += static_cast<double>(corner.z) * inverseCount;
	}

	using namespace DirectX;

	XMFLOAT3 forward = GetForward();

	if (!MathUtils::IsFinite(forward)) {
		return false;
	}

	const XMVECTOR forwardVector = XMVector3Normalize(XMLoadFloat3(&forward));

	const XMVECTOR rightVector = XMVector3Normalize(
	    XMVector3Cross(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), forwardVector));

	const XMVECTOR upVector =
	    XMVector3Normalize(XMVector3Cross(forwardVector, rightVector));

	XMFLOAT3 right;
	XMFLOAT3 up;

	XMStoreFloat3(&forward, forwardVector);
	XMStoreFloat3(&right, rightVector);
	XMStoreFloat3(&up, upVector);

	if (!MathUtils::IsFinite(forward) || !MathUtils::IsFinite(right) ||
	    !MathUtils::IsFinite(up)) {
		return false;
	}

	const double nearLimit = nearClip * kFrameClipPadding;
	const double farLimit = farClip / kFrameClipPadding;

	double distance = kMinimumFrameDistance;
	double maximumDepth = (std::numeric_limits<double>::lowest)();

	for (const auto &corner : corners) {
		const double offsetX = static_cast<double>(corner.x) - centerX;
		const double offsetY = static_cast<double>(corner.y) - centerY;
		const double offsetZ = static_cast<double>(corner.z) - centerZ;

		// 中心からのずれを、現在のカメラの向きで表す
		const double x =
		    offsetX * right.x + offsetY * right.y + offsetZ * right.z;

		const double y = offsetX * up.x + offsetY * up.y + offsetZ * up.z;

		const double z =
		    offsetX * forward.x + offsetY * forward.y + offsetZ * forward.z;

		// 各頂点が左右・上下・手前のクリップ面に収まる距離を求める
		distance =
		    (std::max)(distance,
		               std::abs(x) * horizontalScale * kFramePadding - z);

		distance = (std::max)(distance,
		                      std::abs(y) * verticalScale * kFramePadding - z);

		distance = (std::max)(distance, nearLimit - z);

		maximumDepth = (std::max)(maximumDepth, z);
	}

	// 遠方のクリップ面に収まらない場合は、現在位置を維持する
	if (!std::isfinite(distance) || distance + maximumDepth >= farLimit) {
		return false;
	}

	const double newX = centerX - forward.x * distance;
	const double newY = centerY - forward.y * distance;
	const double newZ = centerZ - forward.z * distance;

	const double maximumFloat = (std::numeric_limits<float>::max)();

	if (!std::isfinite(newX) || !std::isfinite(newY) || !std::isfinite(newZ) ||
	    std::abs(newX) > maximumFloat || std::abs(newY) > maximumFloat ||
	    std::abs(newZ) > maximumFloat) {
		return false;
	}

	const XMFLOAT3 newPosition{
	    static_cast<float>(newX),
	    static_cast<float>(newY),
	    static_cast<float>(newZ),
	};

	// GetPoseで有効な視線を作れる座標精度があるか確認する
	const XMFLOAT3 target{
	    newPosition.x + forward.x,
	    newPosition.y + forward.y,
	    newPosition.z + forward.z,
	};

	if (!MathUtils::IsFinite(target)) {
		return false;
	}

	const double directionX = static_cast<double>(target.x) - newPosition.x;
	const double directionY = static_cast<double>(target.y) - newPosition.y;
	const double directionZ = static_cast<double>(target.z) - newPosition.z;

	const double directionLengthSquared = directionX * directionX +
	                                      directionY * directionY +
	                                      directionZ * directionZ;

	const double rightLengthSquared =
	    directionX * directionX + directionZ * directionZ;

	if (directionLengthSquared <= kMinimumPoseLengthSquared ||
	    rightLengthSquared <= kMinimumPoseLengthSquared) {
		return false;
	}

	position_ = newPosition;
	navigating_ = false;
	panning_ = false;
	orbiting_ = false;

	return true;
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
	panning_ = false;
	orbiting_ = false;
}
