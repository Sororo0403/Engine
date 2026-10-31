#include "math/TransformUtils.h"

#include "math/MathUtils.h"

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
