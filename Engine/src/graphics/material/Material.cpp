#include "graphics/material/Material.h"
#include "core/Logger.h"
#include "math/MathUtils.h"

#include <cmath>

bool Material::Initialize(ID3D12Device *device,
                          D3D12_GPU_DESCRIPTOR_HANDLE textureSrv) {
	if (!device || textureSrv.ptr == 0 || constantBuffer_.IsInitialized()) {
		Logger::Error(
		    "Material::Initialize: 引数が無効、または初期化済みです。");
		return false;
	}

	static_assert(sizeof(MaterialData) == 32);

	if (!constantBuffer_.Initialize(device)) {
		return false;
	}

	textureSrv_ = textureSrv;

	Logger::Info("Materialを初期化しました。");
	return true;
}

bool Material::Update(const MaterialData &data) {
	if (!constantBuffer_.IsInitialized()) {
		Logger::Error("Material::Update: 未初期化です。");
		return false;
	}

	const float channels[] = {data.baseColor.x, data.baseColor.y,
	                          data.baseColor.z, data.baseColor.w};

	for (const float channel : channels) {
		if (!std::isfinite(channel) || channel < 0.0f || channel > 1.0f) {
			Logger::Error("Material::Update: 色の値が無効です。");
			return false;
		}
	}

	if (!MathUtils::IsFinite(data.uvScale) ||
	    !MathUtils::IsFinite(data.uvOffset)) {
		Logger::Error("Material::Update: UVの値が無効です。");
		return false;
	}

	return constantBuffer_.Update(data);
}

D3D12_GPU_DESCRIPTOR_HANDLE Material::GetTextureSrv() const {
	return textureSrv_;
}

D3D12_GPU_VIRTUAL_ADDRESS
Material::GetConstantBufferAddress() const {
	return constantBuffer_.GetGPUVirtualAddress();
}

void Material::Finalize() {
	constantBuffer_.Finalize();
	textureSrv_ = {};
}
