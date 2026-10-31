#include "graphics/DirectionalLight.h"
#include "core/Logger.h"
#include "math/MathUtils.h"

#include <cmath>

bool DirectionalLight::Initialize(ID3D12Device *device) {
	if (!device || constantBuffer_.IsInitialized()) {
		Logger::Error(
		    "DirectionalLight::Initialize: 引数が無効、または初期化済みです。");
		return false;
	}

	static_assert(sizeof(DirectionalLightData) == 32);

	if (!constantBuffer_.Initialize(device)) {
		return false;
	}

	Logger::Info("平行光源を初期化しました。");
	return true;
}

bool DirectionalLight::Update(const DirectionalLightData &data) {
	if (!constantBuffer_.IsInitialized()) {
		Logger::Error("DirectionalLight::Update: 未初期化です。");
		return false;
	}

	const float lengthSquared = data.direction.x * data.direction.x +
	                            data.direction.y * data.direction.y +
	                            data.direction.z * data.direction.z;

	if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0f ||
	    !std::isfinite(data.intensity) || data.intensity < 0.0f ||
	    !std::isfinite(data.ambient) || data.ambient < 0.0f ||
	    !MathUtils::IsFinite(data.color) || data.color.x < 0.0f ||
	    data.color.y < 0.0f || data.color.z < 0.0f) {
		Logger::Error("DirectionalLight::Update: 光源データが無効です。");
		return false;
	}

	return constantBuffer_.Update(data);
}

D3D12_GPU_VIRTUAL_ADDRESS
DirectionalLight::GetConstantBufferAddress() const {
	return constantBuffer_.GetGPUVirtualAddress();
}

void DirectionalLight::Finalize() { constantBuffer_.Finalize(); }
