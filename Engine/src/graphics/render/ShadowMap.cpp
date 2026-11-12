#include "graphics/render/ShadowMap.h"
#include "graphics/material/MaterialResource.h"
#include "graphics/render/D3D12Graphics.h"
#include "math/MathUtils.h"
#include <limits>

bool ShadowMap::Initialize(D3D12Graphics &graphics) {
	auto *device = graphics.GetDevice();
	if (!device || device_)
		return false;
	D3D12_DESCRIPTOR_HEAP_DESC heap{};
	heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	heap.NumDescriptors = 1;
	if (FAILED(device->CreateDescriptorHeap(
	        &heap, IID_PPV_ARGS(dsv_.GetAddressOf()))) ||
	    !constants_.Initialize(graphics.GetConstantBuffers()))
		return false;
	device_ = device;
	stride_ = device->GetDescriptorHandleIncrementSize(
	    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	return true;
}

bool ShadowMap::Update(const DirectX::XMFLOAT4X4 &lightViewProjection,
                       const ShadowSettings &settings,
                       std::span<const MaterialResource *const> materials) {
	enabled_ = false;
	if (!device_ || !settings.IsValid() ||
	    !MathUtils::IsFinite(lightViewProjection))
		return false;
	Data data;
	data.matrix = lightViewProjection;
	const UINT size =
	    settings.enabled ? static_cast<UINT>(settings.resolution) : 1;
	if (size != resolution_) {
		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = size;
		desc.Height = size;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_R32_TYPELESS;
		desc.SampleDesc.Count = 1;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		D3D12_CLEAR_VALUE clear{};
		clear.Format = DXGI_FORMAT_D32_FLOAT;
		clear.DepthStencil.Depth = 1;
		Microsoft::WRL::ComPtr<ID3D12Resource> candidate;
		if (FAILED(device_->CreateCommittedResource(
		        &heap, D3D12_HEAP_FLAG_NONE, &desc,
		        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
		        IID_PPV_ARGS(candidate.GetAddressOf()))))
			return false;
		D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
		dsv.Format = DXGI_FORMAT_D32_FLOAT;
		dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		device_->CreateDepthStencilView(
		    candidate.Get(), &dsv, dsv_->GetCPUDescriptorHandleForHeapStart());
		depth_ = std::move(candidate);
		resolution_ = size;
	}

	materials_.clear();

	for (const auto *material : materials) {
		if (!material)
			return false;
		if (!materials_.contains(material)) {
			if (materials_.size() >=
			    D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1 - 1)
				return false;
			materials_.emplace(material,
			                   static_cast<UINT>(materials_.size()) + 1);
		}
	}

	const auto required = static_cast<UINT>(materials_.size()) + 1;

	if (required > capacity_) {
		D3D12_DESCRIPTOR_HEAP_DESC heap{};
		heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heap.NumDescriptors = required;
		heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> candidate;
		if (FAILED(device_->CreateDescriptorHeap(
		        &heap, IID_PPV_ARGS(candidate.GetAddressOf()))))
			return false;
		views_ = std::move(candidate);
		capacity_ = required;
	}

	const auto start = views_->GetCPUDescriptorHandleForHeapStart();
	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = DXGI_FORMAT_R32_FLOAT;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = 1;
	device_->CreateShaderResourceView(depth_.Get(), &srv, start);

	for (const auto &[material, index] : materials_) {
		auto handle = start;
		handle.ptr += static_cast<SIZE_T>(index) * stride_;
		if (!material->CreateTextureView(device_, handle))
			return false;
	}

	data.enabled = settings.enabled;
	data.strength = settings.strength;
	data.bias = settings.bias;
	data.normalBias = settings.normalBias;
	data.texelSize = 1.0f / static_cast<float>(resolution_);
	if (!constants_.Update(data))
		return false;
	enabled_ = settings.enabled;
	return true;
}

bool ShadowMap::Begin(ID3D12GraphicsCommandList *commands) const {
	if (!commands || !enabled_ || !depth_)
		return false;
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition = {depth_.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
	                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
	                      D3D12_RESOURCE_STATE_DEPTH_WRITE};
	commands->ResourceBarrier(1, &barrier);
	const auto handle = dsv_->GetCPUDescriptorHandleForHeapStart();
	commands->ClearDepthStencilView(handle, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0,
	                                nullptr);
	commands->OMSetRenderTargets(0, nullptr, FALSE, &handle);
	const D3D12_VIEWPORT viewport{
	    0, 0, static_cast<float>(resolution_), static_cast<float>(resolution_),
	    0, 1};
	const D3D12_RECT scissor{0, 0, static_cast<LONG>(resolution_),
	                         static_cast<LONG>(resolution_)};
	commands->RSSetViewports(1, &viewport);
	commands->RSSetScissorRects(1, &scissor);
	return true;
}

void ShadowMap::End(ID3D12GraphicsCommandList *commands) const {
	if (!commands || !enabled_ || !depth_)
		return;
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition = {depth_.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
	                      D3D12_RESOURCE_STATE_DEPTH_WRITE,
	                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
	commands->ResourceBarrier(1, &barrier);
}

D3D12_GPU_VIRTUAL_ADDRESS ShadowMap::GetConstantBufferAddress() const {
	return constants_.GetGPUVirtualAddress();
}

ID3D12DescriptorHeap *ShadowMap::GetHeap() const {
	return views_.Get();
}

D3D12_GPU_DESCRIPTOR_HANDLE
ShadowMap::GetTextureSrv(const MaterialResource *material) const {
	const auto found = materials_.find(material);
	if (!views_ || found == materials_.end())
		return {};
	auto handle = views_->GetGPUDescriptorHandleForHeapStart();
	handle.ptr += static_cast<UINT64>(found->second) * stride_;
	return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE ShadowMap::GetShadowSrv() const {
	return views_ ? views_->GetGPUDescriptorHandleForHeapStart()
	              : D3D12_GPU_DESCRIPTOR_HANDLE{};
}
