#include "graphics/lighting/DirectionalLight.h"
#include "core/Logger.h"
#include "math/MathUtils.h"

#include <cmath>
#include <cstddef>

bool DirectionalLight::Initialize(ConstantBufferPool &pool) {
	if (constantBuffer_.IsInitialized()) {
		Logger::Error(
		    "DirectionalLight::Initialize: 引数が無効、または初期化済みです。");
		return false;
	}

	static_assert(sizeof(DirectionalLightData) == 32);
	static_assert(offsetof(DirectionalLightsData, lights) == 16);
	static_assert(sizeof(DirectionalLightsData) ==
	              16 + 32 * DirectionalLightsData::kMaximumLights);

	if (!constantBuffer_.Initialize(pool)) {
		return false;
	}

	Logger::Info("平行光源を初期化しました。");
	return true;
}

bool DirectionalLight::Update(const DirectionalLightsData &data) {
	if (!constantBuffer_.IsInitialized()) {
		Logger::Error("DirectionalLight::Update: 未初期化です。");
		return false;
	}

	if (data.count > data.lights.size() || data.shadowIndex < -1 ||
	    data.shadowIndex >= static_cast<std::int32_t>(data.count) ||
	    !std::isfinite(data.ambient) || data.ambient < 0.0f) {
		Logger::Error("DirectionalLight::Update: 光源データが無効です。");
		return false;
	}

	for (std::uint32_t i = 0; i < data.count; ++i) {
		const auto &light = data.lights[i];
		const auto &direction = light.direction;
		const float lengthSquared = direction.x * direction.x +
		                            direction.y * direction.y +
		                            direction.z * direction.z;

		if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0f ||
		    !std::isfinite(light.intensity) || light.intensity < 0.0f ||
		    !MathUtils::IsFinite(light.color) || light.color.x < 0.0f ||
		    light.color.y < 0.0f || light.color.z < 0.0f) {
			Logger::Error("DirectionalLight::Update: 光源データが無効です。");
			return false;
		}
	}

	return constantBuffer_.Update(data);
}

D3D12_GPU_VIRTUAL_ADDRESS
DirectionalLight::GetConstantBufferAddress() const {
	return constantBuffer_.GetGPUVirtualAddress();
}

void DirectionalLight::Finalize() {
	constantBuffer_.Finalize();
}
