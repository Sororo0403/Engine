#include "math/TransformUtils.h"

#include "math/MathUtils.h"

#include <algorithm>
#include <cmath>

bool TransformUtils::IsValid(const Transform &transform) {
	const auto &q = transform.rotation;
	const double lengthSquared =
	    static_cast<double>(q.x) * q.x + static_cast<double>(q.y) * q.y +
	    static_cast<double>(q.z) * q.z + static_cast<double>(q.w) * q.w;

	return MathUtils::IsFinite(transform.translation) &&
	       MathUtils::IsFinite(transform.rotation) &&
	       std::abs(lengthSquared - 1.0) <= 0.0001 &&
	       MathUtils::IsFinite(transform.scale) && transform.scale.x != 0.0f &&
	       transform.scale.y != 0.0f && transform.scale.z != 0.0f;
}

bool TransformUtils::TryNormalizeRotation(
    const DirectX::XMFLOAT4 &rotation, DirectX::XMFLOAT4 &normalized) {
	if (!MathUtils::IsFinite(rotation)) {
		return false;
	}

	// doubleで計算し、floatの大きな値や小さな値でも長さを求める
	const double length = std::sqrt(
	    static_cast<double>(rotation.x) * rotation.x +
	    static_cast<double>(rotation.y) * rotation.y +
	    static_cast<double>(rotation.z) * rotation.z +
	    static_cast<double>(rotation.w) * rotation.w);

	if (length == 0.0) {
		return false;
	}

	normalized = {
	    static_cast<float>(rotation.x / length),
	    static_cast<float>(rotation.y / length),
	    static_cast<float>(rotation.z / length),
	    static_cast<float>(rotation.w / length),
	};
	return true;
}

DirectX::XMFLOAT4 TransformUtils::RotationFromEuler(
    const DirectX::XMFLOAT3 &radians) {
	DirectX::XMFLOAT4 rotation;
	DirectX::XMStoreFloat4(
	    &rotation, DirectX::XMQuaternionNormalize(
	                   DirectX::XMQuaternionRotationRollPitchYaw(
	                       radians.x, radians.y, radians.z)));
	return rotation;
}

DirectX::XMFLOAT3 TransformUtils::RotationToEuler(
    const DirectX::XMFLOAT4 &rotation) {
	// 特異点付近で小さな行列成分の精度を失わないようdoubleで求める
	const double length = std::sqrt(
	    static_cast<double>(rotation.x) * rotation.x +
	    static_cast<double>(rotation.y) * rotation.y +
	    static_cast<double>(rotation.z) * rotation.z +
	    static_cast<double>(rotation.w) * rotation.w);
	const double x = rotation.x / length;
	const double y = rotation.y / length;
	const double z = rotation.z / length;
	const double w = rotation.w / length;

	const double m31 = 2.0 * (x * z + w * y);
	const double m32 = 2.0 * (y * z - w * x);
	const double m33 = 1.0 - 2.0 * (x * x + y * y);
	const double cosPitch = std::hypot(m31, m33);

	// XMQuaternionRotationRollPitchYawと同じ回転順序へ戻す
	DirectX::XMFLOAT3 radians;
	radians.x = static_cast<float>(std::atan2(-m32, cosPitch));

	if (cosPitch > 0.000001) {
		const double m12 = 2.0 * (x * y + w * z);
		const double m22 = 1.0 - 2.0 * (x * x + z * z);
		radians.y = static_cast<float>(std::atan2(m31, m33));
		radians.z = static_cast<float>(std::atan2(m12, m22));
	} else {
		// 特異点ではZ回転を0として等価な回転を求める
		const double m13 = 2.0 * (x * z - w * y);
		const double m11 = 1.0 - 2.0 * (y * y + z * z);
		radians.y = static_cast<float>(std::atan2(-m13, m11));
		radians.z = 0.0f;
	}

	return radians;
}

DirectX::XMMATRIX TransformUtils::MakeWorldMatrix(const Transform &transform) {
	using namespace DirectX;

	const XMMATRIX scale = XMMatrixScaling(transform.scale.x, transform.scale.y,
	                                       transform.scale.z);
	const XMMATRIX rotation = XMMatrixRotationQuaternion(
	    XMQuaternionNormalize(XMLoadFloat4(&transform.rotation)));
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

	Transform result;
	XMStoreFloat3(&result.scale, scale);
	XMStoreFloat3(&result.translation, translation);
	XMStoreFloat4(&result.rotation, rotation);

	if (!TryNormalizeRotation(result.rotation, result.rotation)) {
		return false;
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

bool TransformUtils::AreEqual(const Transform &left, const Transform &right) {
	const auto equal = [](const DirectX::XMFLOAT3 &a,
	                      const DirectX::XMFLOAT3 &b) {
		return a.x == b.x && a.y == b.y && a.z == b.z;
	};

	if (!IsValid(left) || !IsValid(right)) {
		return false;
	}

	DirectX::XMFLOAT4 a;
	DirectX::XMFLOAT4 b;
	if (!TryNormalizeRotation(left.rotation, a) ||
	    !TryNormalizeRotation(right.rotation, b)) {
		return false;
	}

	// qと-qは同じ回転を表すため、両方の符号で比較する
	const auto close = [](float x, float y) {
		return std::abs(x - y) <= 0.000001f;
	};
	const bool same = close(a.x, b.x) && close(a.y, b.y) &&
	                  close(a.z, b.z) && close(a.w, b.w);
	const bool opposite = close(a.x, -b.x) && close(a.y, -b.y) &&
	                      close(a.z, -b.z) && close(a.w, -b.w);

	return equal(left.translation, right.translation) &&
	       equal(left.scale, right.scale) && (same || opposite);
}
