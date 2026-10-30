#include "graphics/Material.h"
#include "core/Logger.h"

#include <directxtk12/BufferHelpers.h>

#include <cmath>
#include <format>

bool Material::Initialize(ID3D12Device *device,
                          D3D12_GPU_DESCRIPTOR_HANDLE textureSrv) {
	if (!device || textureSrv.ptr == 0 || constantBuffer_) {
		Logger::Error(
		    "Material::Initialize: 引数が無効、または初期化済みです。");
		return false;
	}

	static_assert(sizeof(MaterialData) == 32);

	HRESULT hr = DirectX::CreateUploadBuffer(
	    device, nullptr, 1, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,
	    constantBuffer_.GetAddressOf());

	if (FAILED(hr)) {
		Logger::Error(std::format(
		    "材質用の定数バッファ作成に失敗しました。HRESULT=0x{:08X}",
		    static_cast<unsigned long>(hr)));
		return false;
	}

	void *mapped = nullptr;
	const D3D12_RANGE readRange{0, 0};

	hr = constantBuffer_->Map(0, &readRange, &mapped);

	if (FAILED(hr)) {
		Logger::Error(std::format(
		    "材質用の定数バッファのMapに失敗しました。HRESULT=0x{:08X}",
		    static_cast<unsigned long>(hr)));

		Finalize();
		return false;
	}

	mappedData_ = static_cast<MaterialData *>(mapped);
	textureSrv_ = textureSrv;

	*mappedData_ = MaterialData{};

	Logger::Info("Materialを初期化しました。");
	return true;
}

bool Material::Update(const MaterialData &data) {
	if (!mappedData_) {
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

	const float uvValues[] = {data.uvScale.x, data.uvScale.y, data.uvOffset.x,
	                          data.uvOffset.y};

	for (const float value : uvValues) {
		if (!std::isfinite(value)) {
			Logger::Error("Material::Update: UVの値が無効です。");
			return false;
		}
	}

	*mappedData_ = data;
	return true;
}

D3D12_GPU_DESCRIPTOR_HANDLE Material::GetTextureSrv() const {
	return textureSrv_;
}

D3D12_GPU_VIRTUAL_ADDRESS
Material::GetConstantBufferAddress() const {
	if (!constantBuffer_) {
		return 0;
	}

	return constantBuffer_->GetGPUVirtualAddress();
}

void Material::Finalize() {
	if (constantBuffer_ && mappedData_) {
		constantBuffer_->Unmap(0, nullptr);
	}

	mappedData_ = nullptr;
	constantBuffer_.Reset();
	textureSrv_ = {};
}
