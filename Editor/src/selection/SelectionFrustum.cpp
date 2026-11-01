#include "selection/SelectionFrustum.h"

#include "math/MathUtils.h"

#include <cmath>

bool SelectionFrustum::Initialize(
    const DirectX::XMFLOAT4X4 &view,
    const DirectX::XMFLOAT4X4 &projection,
    const DirectX::XMFLOAT2 &minimum,
    const DirectX::XMFLOAT2 &maximum) {
	initialized_ = false;
	if (!MathUtils::IsFinite(minimum) || !MathUtils::IsFinite(maximum) ||
	    minimum.x < 0.0f || minimum.y < 0.0f || maximum.x > 1.0f ||
	    maximum.y > 1.0f || minimum.x >= maximum.x || minimum.y >= maximum.y) {
		return false;
	}
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			if (!std::isfinite(view.m[row][column]) ||
			    !std::isfinite(projection.m[row][column])) {
				return false;
			}
		}
	}
	if (projection._11 <= 0.0f || projection._22 <= 0.0f ||
	    projection._33 <= 1.0f || projection._43 >= 0.0f ||
	    projection._34 != 1.0f || projection._44 != 0.0f) {
		return false;
	}

	using namespace DirectX;
	XMVECTOR determinant;
	const XMMATRIX inverseView =
	    XMMatrixInverse(&determinant, XMLoadFloat4x4(&view));
	if (!std::isfinite(XMVectorGetX(determinant)) ||
	    std::abs(XMVectorGetX(determinant)) < 0.000001f) {
		return false;
	}

	BoundingFrustum cameraFrustum;
	BoundingFrustum::CreateFromMatrix(cameraFrustum,
	                                 XMLoadFloat4x4(&projection));
	if (!std::isfinite(cameraFrustum.Near) ||
	    !std::isfinite(cameraFrustum.Far) || cameraFrustum.Near <= 0.0f ||
	    cameraFrustum.Far <= cameraFrustum.Near) {
		return false;
	}

	const float left = cameraFrustum.LeftSlope;
	const float right = cameraFrustum.RightSlope;
	const float top = cameraFrustum.TopSlope;
	const float bottom = cameraFrustum.BottomSlope;
	cameraFrustum.LeftSlope = std::lerp(left, right, minimum.x);
	cameraFrustum.RightSlope = std::lerp(left, right, maximum.x);
	cameraFrustum.TopSlope = std::lerp(top, bottom, minimum.y);
	cameraFrustum.BottomSlope = std::lerp(top, bottom, maximum.y);

	// 画面の矩形をカメラのNearからFarまで延ばし、ワールド座標へ戻す
	cameraFrustum.Transform(frustum_, inverseView);
	initialized_ = true;
	return true;
}

bool SelectionFrustum::Intersects(
    const std::array<DirectX::XMFLOAT3, 8> &corners) const {
	if (!initialized_) {
		return false;
	}
	for (const auto &corner : corners) {
		if (!MathUtils::IsFinite(corner)) {
			return false;
		}
	}

	DirectX::BoundingOrientedBox bounds;
	DirectX::BoundingOrientedBox::CreateFromPoints(
	    bounds, corners.size(), corners.data(), sizeof(corners.front()));
	if (!MathUtils::IsFinite(bounds.Center) ||
	    !MathUtils::IsFinite(bounds.Extents) ||
	    !MathUtils::IsFinite(bounds.Orientation)) {
		return false;
	}
	return frustum_.Intersects(bounds);
}
