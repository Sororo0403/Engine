#include "graphics/DirectionalLight.h"
#include "core/Logger.h"

#include <directxtk12/BufferHelpers.h>

#include <cmath>
#include <format>

bool DirectionalLight::Initialize(ID3D12Device *device) {
	if (!device || constantBuffer_) {
		Logger::Error(
		    "DirectionalLight::Initialize: 引数が無効、または初期化済みです。");
		return false;
	}

	static_assert(sizeof(DirectionalLightData) == 32);

	// 定数バッファ用に256バイト確保する
	HRESULT hr = DirectX::CreateUploadBuffer(
	    device, nullptr, 1, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,
	    constantBuffer_.GetAddressOf());

	if (FAILED(hr)) {
		Logger::Error(std::format(
		    "光源用の定数バッファ作成に失敗しました。HRESULT=0x{:08X}",
		    static_cast<unsigned long>(hr)));
		return false;
	}

	void *mapped = nullptr;
	const D3D12_RANGE readRange{0, 0};

	hr = constantBuffer_->Map(0, &readRange, &mapped);

	if (FAILED(hr)) {
		Logger::Error(std::format(
		    "光源用の定数バッファのMapに失敗しました。HRESULT=0x{:08X}",
		    static_cast<unsigned long>(hr)));

		Finalize();
		return false;
	}

	mappedData_ = static_cast<DirectionalLightData *>(mapped);

	// 初期値を書き込む
	*mappedData_ = DirectionalLightData{};

	Logger::Info("平行光源を初期化しました。");
	return true;
}

bool DirectionalLight::Update(const DirectionalLightData &data) {
	if (!mappedData_) {
		Logger::Error("DirectionalLight::Update: 未初期化です。");
		return false;
	}

	const float lengthSquared = data.direction.x * data.direction.x +
	                            data.direction.y * data.direction.y +
	                            data.direction.z * data.direction.z;

	if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0f ||
	    !std::isfinite(data.intensity) || data.intensity < 0.0f ||
	    !std::isfinite(data.ambient) || data.ambient < 0.0f ||
	    !std::isfinite(data.color.x) || data.color.x < 0.0f ||
	    !std::isfinite(data.color.y) || data.color.y < 0.0f ||
	    !std::isfinite(data.color.z) || data.color.z < 0.0f) {
		Logger::Error("DirectionalLight::Update: 光源データが無効です。");
		return false;
	}

	*mappedData_ = data;
	return true;
}

D3D12_GPU_VIRTUAL_ADDRESS
DirectionalLight::GetConstantBufferAddress() const {
	if (!constantBuffer_) {
		return 0;
	}

	return constantBuffer_->GetGPUVirtualAddress();
}

void DirectionalLight::Finalize() {
	if (constantBuffer_ && mappedData_) {
		constantBuffer_->Unmap(0, nullptr);
	}

	mappedData_ = nullptr;
	constantBuffer_.Reset();
}
