#include "graphics/camera/Camera.h"

#include "math/MathUtils.h"
#include "math/Ray.h"

#include <cmath>

bool Camera::Update(int width, int height) {
	if (width <= 0 || height <= 0) {
		return false;
	}

	using namespace DirectX;

	// カメラの位置・注視点・上方向からビュー行列を作る
	const XMMATRIX view = XMMatrixLookAtLH(
	    XMLoadFloat3(&position_), XMLoadFloat3(&target_), XMLoadFloat3(&up_));

	const float aspectRatio =
	    static_cast<float>(width) / static_cast<float>(height);

	// 縦方向の視野角・縦横比・表示する距離から投影行列を作る
	const XMMATRIX projection = XMMatrixPerspectiveFovLH(
	    XMConvertToRadians(settings_.verticalFovDegrees), aspectRatio,
	    settings_.nearClip, settings_.farClip);

	XMStoreFloat4x4(&viewMatrix_, view);
	XMStoreFloat4x4(&projectionMatrix_, projection);

	return true;
}

const DirectX::XMFLOAT4X4 &Camera::GetViewMatrix() const { return viewMatrix_; }

const DirectX::XMFLOAT4X4 &Camera::GetProjectionMatrix() const {
	return projectionMatrix_;
}

void Camera::Translate(const DirectX::XMFLOAT3 &offset) {
	position_.x += offset.x;
	position_.y += offset.y;
	position_.z += offset.z;

	target_.x += offset.x;
	target_.y += offset.y;
	target_.z += offset.z;
}

bool Camera::SetPose(const CameraPose &pose) {
	if (!MathUtils::IsFinite(pose.position) ||
	    !MathUtils::IsFinite(pose.target) || !MathUtils::IsFinite(pose.up)) {
		return false;
	}

	using namespace DirectX;

	const XMVECTOR direction = XMVectorSubtract(XMLoadFloat3(&pose.target),
	                                            XMLoadFloat3(&pose.position));

	const XMVECTOR right = XMVector3Cross(XMLoadFloat3(&pose.up), direction);

	const float directionLengthSquared =
	    XMVectorGetX(XMVector3LengthSq(direction));

	const float rightLengthSquared = XMVectorGetX(XMVector3LengthSq(right));

	// 視線がゼロ、または上方向と平行な場合は設定しない
	if (!std::isfinite(directionLengthSquared) ||
	    !std::isfinite(rightLengthSquared) ||
	    directionLengthSquared <= 0.00000001f ||
	    rightLengthSquared <= 0.00000001f) {
		return false;
	}

	position_ = pose.position;
	target_ = pose.target;
	up_ = pose.up;

	return true;
}

bool Camera::CreatePickingRay(float imageX, float imageY, Ray &ray) const {
	if (!std::isfinite(imageX) || !std::isfinite(imageY) || imageX < 0.0f ||
	    imageX > 1.0f || imageY < 0.0f || imageY > 1.0f) {
		return false;
	}

	using namespace DirectX;

	const XMMATRIX view = XMLoadFloat4x4(&viewMatrix_);
	const XMMATRIX projection = XMLoadFloat4x4(&projectionMatrix_);

	XMVECTOR determinant;
	const XMMATRIX inverseViewProjection =
	    XMMatrixInverse(&determinant, view * projection);

	const float determinantValue = XMVectorGetX(determinant);

	if (!std::isfinite(determinantValue) || determinantValue == 0.0f) {
		return false;
	}

	// UIの左上原点を、クリップ空間の座標へ変換する
	const float x = imageX * 2.0f - 1.0f;
	const float y = 1.0f - imageY * 2.0f;

	const XMVECTOR nearPoint = XMVector3TransformCoord(
	    XMVectorSet(x, y, 0.0f, 1.0f), inverseViewProjection);

	const XMVECTOR farPoint = XMVector3TransformCoord(
	    XMVectorSet(x, y, 1.0f, 1.0f), inverseViewProjection);

	const XMVECTOR difference = XMVectorSubtract(farPoint, nearPoint);

	const float length = XMVectorGetX(XMVector3Length(difference));

	if (!std::isfinite(length) || length <= 0.0f) {
		return false;
	}

	Ray result;
	XMStoreFloat3(&result.origin, nearPoint);
	XMStoreFloat3(&result.direction, XMVectorScale(difference, 1.0f / length));

	result.maxDistance = length;

	if (!MathUtils::IsFinite(result.origin) ||
	    !MathUtils::IsFinite(result.direction)) {
		return false;
	}

	ray = result;
	return true;
}

bool Camera::SetSettings(const CameraSettings &settings) {
	if (!settings.IsValid()) {
		return false;
	}
	settings_ = settings;
	return true;
}
