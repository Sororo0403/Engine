#include "graphics/lighting/SpotLights.h"

#include <algorithm>
#include <cstddef>

bool SpotLights::Initialize(ConstantBufferPool &pool) {
	static_assert(sizeof(SpotLightData) == 64);
	static_assert(offsetof(SpotLightData, direction) == 16);
	static_assert(offsetof(SpotLightData, color) == 32);
	static_assert(offsetof(SpotLightData, cosOuter) == 48);
	static_assert(offsetof(Constants, lights) == 16);
	static_assert(sizeof(Constants) == 2064);
	return !constantBuffer_.IsInitialized() &&
	       constantBuffer_.Initialize(pool, Constants{});
}

bool SpotLights::Update(std::span<const SpotLightData> lights) {
	if (!constantBuffer_.IsInitialized() ||
	    lights.size() > SpotLightData::kMaximumLights ||
	    !std::all_of(lights.begin(), lights.end(),
	                 [](const auto &light) { return light.IsValid(); })) {
		return false;
	}

	Constants data;
	data.count = static_cast<std::uint32_t>(lights.size());
	std::copy(lights.begin(), lights.end(), data.lights.begin());
	return constantBuffer_.Update(data);
}

D3D12_GPU_VIRTUAL_ADDRESS SpotLights::GetConstantBufferAddress() const {
	return constantBuffer_.GetGPUVirtualAddress();
}

void SpotLights::Finalize() {
	constantBuffer_.Finalize();
}
