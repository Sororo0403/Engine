#include "graphics/Camera.h"

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
	const XMMATRIX projection =
	    XMMatrixPerspectiveFovLH(fovY_, aspectRatio, nearZ_, farZ_);

	XMStoreFloat4x4(&viewMatrix_, view);
	XMStoreFloat4x4(&projectionMatrix_, projection);

	return true;
}

const DirectX::XMFLOAT4X4 &Camera::GetViewMatrix() const { return viewMatrix_; }

const DirectX::XMFLOAT4X4 &Camera::GetProjectionMatrix() const {
	return projectionMatrix_;
}
