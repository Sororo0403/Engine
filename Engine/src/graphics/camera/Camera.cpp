#include "graphics/camera/Camera.h"

#include "math/MathUtils.h"
#include "math/Ray.h"

#include <cmath>
#include <limits>

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
	projectedSettings_ = settings_;
	viewOrigin_ = position_;

	return true;
}

const DirectX::XMFLOAT4X4 &Camera::GetViewMatrix() const {
	return viewMatrix_;
}

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

	const double horizontal = projectionMatrix_._11;
	const double vertical = projectionMatrix_._22;
	if (!std::isfinite(horizontal) || !std::isfinite(vertical) ||
	    horizontal <= 0 || vertical <= 0)
		return false;

	// カメラ空間のZ=1を通る方向を作る。遠クリップの逆投影で生じる桁落ちを避ける。
	const double x = (double(imageX) * 2 - 1) / horizontal;
	const double y = (1 - double(imageY) * 2) / vertical;
	const auto &view = viewMatrix_;
	const double dx = x * view._11 + y * view._12 + view._13;
	const double dy = x * view._21 + y * view._22 + view._23;
	const double dz = x * view._31 + y * view._32 + view._33;
	const double length = std::hypot(dx, dy, dz);
	const double distance = length * (double(projectedSettings_.farClip) -
	                                  projectedSettings_.nearClip);
	constexpr double limit = (std::numeric_limits<float>::max)();
	if (!std::isfinite(distance) || distance <= 0 || distance > limit)
		return false;

	const double near = projectedSettings_.nearClip;
	const double ox = double(viewOrigin_.x) + dx * near;
	const double oy = double(viewOrigin_.y) + dy * near;
	const double oz = double(viewOrigin_.z) + dz * near;
	if (!std::isfinite(ox) || !std::isfinite(oy) || !std::isfinite(oz) ||
	    std::abs(ox) > limit || std::abs(oy) > limit || std::abs(oz) > limit)
		return false;

	const Ray result{
	    {float(ox), float(oy), float(oz)},
	    {float(dx / length), float(dy / length), float(dz / length)},
	    float(distance)};

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

std::optional<ViewportPoint>
Camera::Project(const DirectX::XMFLOAT3 &position) const {
	if (!MathUtils::IsFinite(position))
		return std::nullopt;
	const double source[]{position.x, position.y, position.z, 1};
	double view[4]{}, clip[4]{};
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
			view[i] += source[j] * viewMatrix_.m[j][i];
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
			clip[i] += view[j] * projectionMatrix_.m[j][i];
	if (!std::isfinite(clip[3]) || clip[3] <= 0)
		return std::nullopt;
	const double x = (clip[0] / clip[3] + 1) * .5;
	const double y = (1 - clip[1] / clip[3]) * .5;
	constexpr double limit = (std::numeric_limits<float>::max)();
	if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > limit ||
	    std::abs(y) > limit || clip[3] > limit)
		return std::nullopt;
	const bool visible = x >= 0 && x <= 1 && y >= 0 && y <= 1 &&
	                     view[2] >= projectedSettings_.nearClip &&
	                     view[2] <= projectedSettings_.farClip;
	return ViewportPoint{float(x), float(y), float(clip[3]), visible};
}
