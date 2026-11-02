#include "graphics/material/Material.h"
#include "core/Logger.h"
#include "graphics/material/MaterialResource.h"

#include <cmath>
#include <cstddef>

bool Material::Initialize(ID3D12Device *device,
                          std::shared_ptr<const MaterialResource> resource) {
	if (!device || !resource || resource->GetTextureSrv().ptr == 0 ||
	    constantBuffer_.IsInitialized()) {
		Logger::Error(
		    "Material::Initialize: 引数が無効、または初期化済みです。");
		return false;
	}

	static_assert(sizeof(MaterialData) == 48);
	static_assert(offsetof(MaterialData, baseColor) == 0);
	static_assert(offsetof(MaterialData, uvScale) == 16);
	static_assert(offsetof(MaterialData, uvOffset) == 24);
	static_assert(offsetof(MaterialData, shadingMode) == 32);

	if (!constantBuffer_.Initialize(device)) {
		return false;
	}

	resource_ = std::move(resource);

	Logger::Info("Materialを初期化しました。");
	return true;
}

bool Material::Update(const MaterialData &data) {
	if (!constantBuffer_.IsInitialized()) {
		Logger::Error("Material::Update: 未初期化です。");
		return false;
	}

	if (!data.IsValid()) {
		Logger::Error("Material::Update: 材質の設定が無効です。");
		return false;
	}

	return constantBuffer_.Update(data);
}

D3D12_GPU_DESCRIPTOR_HANDLE Material::GetTextureSrv() const {
	return resource_ ? resource_->GetTextureSrv()
	                 : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

D3D12_GPU_VIRTUAL_ADDRESS
Material::GetConstantBufferAddress() const {
	return constantBuffer_.GetGPUVirtualAddress();
}

void Material::Finalize() {
	constantBuffer_.Finalize();
	resource_.reset();
}

bool Material::SetResource(std::shared_ptr<const MaterialResource> resource) {
	if (!constantBuffer_.IsInitialized() || !resource ||
	    resource->GetTextureSrv().ptr == 0) {
		return false;
	}

	resource_ = std::move(resource);
	return true;
}

const std::shared_ptr<const MaterialResource> &Material::GetResource() const {
	return resource_;
}
