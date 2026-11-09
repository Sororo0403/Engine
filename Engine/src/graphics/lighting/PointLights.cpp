#include "graphics/lighting/PointLights.h"

#include <algorithm>
#include <cstddef>

bool PointLights::Initialize(ID3D12Device *device) {
	static_assert(sizeof(PointLightData) == 32);
	static_assert(offsetof(Constants, lights) == 16);
	static_assert(sizeof(Constants) == 1040);
	return device && !constantBuffer_.IsInitialized() &&
	       constantBuffer_.Initialize(device, Constants{});
}

bool PointLights::Update(std::span<const PointLightData> lights) {
	if (!constantBuffer_.IsInitialized() ||
	    lights.size() > PointLightData::kMaximumLights ||
	    !std::all_of(lights.begin(), lights.end(),
	                 [](const auto &light) { return light.IsValid(); })) {
		return false;
	}

	Constants data;
	data.count = static_cast<std::uint32_t>(lights.size());
	std::copy(lights.begin(), lights.end(), data.lights.begin());
	return constantBuffer_.Update(data);
}

D3D12_GPU_VIRTUAL_ADDRESS PointLights::GetConstantBufferAddress() const {
	return constantBuffer_.GetGPUVirtualAddress();
}

void PointLights::Finalize() {
	constantBuffer_.Finalize();
}
