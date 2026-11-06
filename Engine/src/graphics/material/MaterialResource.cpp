#include "graphics/material/MaterialResource.h"
#include <array>
#include <directxtk12/DirectXHelpers.h>

bool MaterialResource::Initialize(ID3D12Device *device,
                                  ID3D12CommandQueue *queue,
                                  const MaterialData &data,
                                  const TextureData &image) {
	if (!device || !queue || heap_ || texture_.GetResource() ||
	    !data.IsValid() ||
	    queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
		return false;
	}

	bool loaded = false;

	switch (image.source) {
	case TextureSource::NONE: {
		const std::array<std::uint8_t, 4> white{255, 255, 255, 255};
		loaded = texture_.CreateFromRGBA(device, queue, 1, 1, white);
		break;
	}
	case TextureSource::FILE:
		loaded = texture_.Load(device, queue, image.filePath);
		break;
	case TextureSource::ENCODED:
		loaded = texture_.LoadFromMemory(device, queue, image.bytes);
		break;
	case TextureSource::RGBA8:
		loaded = texture_.CreateFromRGBA(device, queue, image.width,
		                                 image.height, image.bytes);
		break;
	}

	if (!loaded) {
		texture_.Finalize();
		return false;
	}

	try {
		auto heap = std::make_unique<DirectX::DescriptorHeap>(device, 1);
		DirectX::CreateShaderResourceView(device, texture_.GetResource(),
		                                  heap->GetCpuHandle(0));
		heap_ = std::move(heap);
		data_ = data;
		return true;
	} catch (...) {
		texture_.Finalize();
		throw;
	}
}

const MaterialData &MaterialResource::GetData() const {
	return data_;
}

ID3D12DescriptorHeap *MaterialResource::GetDescriptorHeap() const {
	return heap_ ? heap_->Heap() : nullptr;
}

D3D12_GPU_DESCRIPTOR_HANDLE MaterialResource::GetTextureSrv() const {
	return heap_ ? heap_->GetGpuHandle(0) : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

bool MaterialResource::CreateTextureView(
    ID3D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE destination) const {
	if (!device || !destination.ptr || !texture_.GetResource())
		return false;
	Microsoft::WRL::ComPtr<ID3D12Device> owner;
	if (FAILED(texture_.GetResource()->GetDevice(
	        IID_PPV_ARGS(owner.GetAddressOf()))) ||
	    owner.Get() != device)
		return false;
	DirectX::CreateShaderResourceView(device, texture_.GetResource(),
	                                  destination);
	return true;
}
