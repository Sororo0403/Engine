#include "math/TransformUtils.h"

#include "math/MathUtils.h"

#include <algorithm>
#include <cmath>

bool TransformUtils::IsValid(const Transform &transform) {
	return MathUtils::IsFinite(transform.translation) &&
	       MathUtils::IsFinite(transform.rotation) &&
	       MathUtils::IsFinite(transform.scale) && transform.scale.x != 0.0f &&
	       transform.scale.y != 0.0f && transform.scale.z != 0.0f;
}

DirectX::XMMATRIX TransformUtils::MakeWorldMatrix(const Transform &transform) {
	using namespace DirectX;

	const XMMATRIX scale = XMMatrixScaling(transform.scale.x, transform.scale.y,
	                                       transform.scale.z);
	const XMMATRIX rotation = XMMatrixRotationRollPitchYaw(
	    transform.rotation.x, transform.rotation.y, transform.rotation.z);
	const XMMATRIX translation =
	    XMMatrixTranslation(transform.translation.x, transform.translation.y,
	                        transform.translation.z);

	return scale * rotation * translation;
}

bool TransformUtils::TryFromWorldMatrix(const DirectX::XMFLOAT4X4 &matrix,
                                        Transform &transform) {
	using namespace DirectX;

	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			if (!std::isfinite(matrix.m[row][column])) {
				return false;
			}
		}
	}

	XMVECTOR scale;
	XMVECTOR rotation;
	XMVECTOR translation;

	if (!XMMatrixDecompose(&scale, &rotation, &translation,
	                       XMLoadFloat4x4(&matrix))) {
		return false;
	}

	XMFLOAT4 quaternion;
	XMStoreFloat4(&quaternion, rotation);

	if (!MathUtils::IsFinite(quaternion)) {
		return false;
	}

	Transform result;
	XMStoreFloat3(&result.scale, scale);
	XMStoreFloat3(&result.translation, translation);

	XMFLOAT4X4 rotationMatrix;
	XMStoreFloat4x4(&rotationMatrix, XMMatrixRotationQuaternion(rotation));

	// XMMatrixRotationRollPitchYawと同じ回転順序へ戻す
	const float sinPitch = std::clamp(-rotationMatrix._32, -1.0f, 1.0f);

	result.rotation.x = std::asin(sinPitch);

	if (std::abs(std::cos(result.rotation.x)) > 0.00001f) {
		result.rotation.y = std::atan2(rotationMatrix._31, rotationMatrix._33);

		result.rotation.z = std::atan2(rotationMatrix._12, rotationMatrix._22);
	} else {
		// ジンバルロック時はZ回転を0として等価な回転を求める
		result.rotation.y = std::atan2(-rotationMatrix._13, rotationMatrix._11);

		result.rotation.z = 0.0f;
	}

	if (!IsValid(result)) {
		return false;
	}

	XMFLOAT4X4 reconstructed;
	XMStoreFloat4x4(&reconstructed, MakeWorldMatrix(result));

	// せん断など、Transformでは保持できない変形を受け入れない
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			const float original = matrix.m[row][column];
			const float restored = reconstructed.m[row][column];

			const float tolerance =
			    0.001f * (std::max)(1.0f, std::abs(original));

			if (!std::isfinite(restored) ||
			    std::abs(original - restored) > tolerance) {
				return false;
			}
		}
	}

	transform = result;
	return true;
}
