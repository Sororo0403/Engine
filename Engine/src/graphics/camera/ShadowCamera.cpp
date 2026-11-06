#include "graphics/camera/ShadowCamera.h"
#include "math/TransformUtils.h"
#include <algorithm>
#include <cmath>

bool ShadowCamera::Build(const DirectX::XMFLOAT4X4 &view,
                         const DirectX::XMFLOAT3 &direction,
                         const ShadowSettings &settings,
                         DirectX::XMFLOAT4X4 &matrix) {
	using namespace DirectX;
	const float scale =
	    (std::max)({std::abs(direction.x), std::abs(direction.y),
	                std::abs(direction.z)});
	XMFLOAT4X4 inverse;
	if (!settings.IsValid() || !std::isfinite(direction.x) ||
	    !std::isfinite(direction.y) || !std::isfinite(direction.z) ||
	    !std::isfinite(scale) || scale <= 0 ||
	    !TransformUtils::TryInverseWorldMatrix(view, inverse))
		return false;
	const auto light = XMVector3Normalize(XMVectorSet(
	    direction.x / scale, direction.y / scale, direction.z / scale, 0));
	const auto up = std::abs(XMVectorGetY(light)) > .99f
	                    ? XMVectorSet(0, 0, 1, 0)
	                    : XMVectorSet(0, 1, 0, 0);
	const auto rotation = XMMatrixLookToLH(XMVectorZero(), light, up);
	const auto camera = XMLoadFloat4x4(&inverse);
	const auto forward = XMVector3Normalize(camera.r[2]);
	const auto center = XMVectorMultiplyAdd(
	    forward, XMVectorReplicate(settings.extent * .5f), camera.r[3]);
	auto snapped = XMVector3TransformCoord(center, rotation);
	const double texel = double(settings.extent) * 2 / settings.resolution;
	snapped = XMVectorSetX(
	    snapped,
	    float(std::floor(double(XMVectorGetX(snapped)) / texel + .5) * texel));
	snapped = XMVectorSetY(
	    snapped,
	    float(std::floor(double(XMVectorGetY(snapped)) / texel + .5) * texel));
	snapped =
	    XMVectorSetZ(snapped, XMVectorGetZ(snapped) - settings.extent * 2);
	const auto eye =
	    XMVector3TransformCoord(snapped, XMMatrixTranspose(rotation));
	XMFLOAT4X4 candidate;
	XMStoreFloat4x4(&candidate,
	                XMMatrixLookToLH(eye, light, up) *
	                    XMMatrixOrthographicLH(settings.extent * 2,
	                                           settings.extent * 2, .01f,
	                                           settings.extent * 4));
	for (const auto &row : candidate.m)
		for (float value : row)
			if (!std::isfinite(value))
				return false;
	matrix = candidate;
	return true;
}
