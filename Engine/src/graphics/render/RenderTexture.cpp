#include "graphics/render/RenderTexture.h"

#include "core/Logger.h"
#include "graphics/render/D3D12Graphics.h"
#include "graphics/render/D3D12Helpers.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <exception>
#include <format>
#include <utility>

/// <summary>
/// 描画先の色・深度バッファと、それぞれのビューを保持する
/// </summary>
struct RenderTexture::Impl {
	explicit Impl(D3D12Graphics &graphics) : graphics(graphics) {}

	/// <summary>
	/// 色・深度バッファとビューを作成する
	/// </summary>
	/// <param name="width">描画領域の幅</param>
	/// <param name="height">描画領域の高さ</param>
	/// <returns>true:成功 false:作成失敗</returns>
	bool Create(int width, int height) {
		auto *device = graphics.GetDevice();

		if (!device) {
			return false;
		}

		D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		heapDesc.NumDescriptors = 1;

		if (FAILED(device->CreateDescriptorHeap(
		        &heapDesc, IID_PPV_ARGS(rtvHeap.GetAddressOf())))) {
			return false;
		}

		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

		if (FAILED(device->CreateDescriptorHeap(
		        &heapDesc, IID_PPV_ARGS(dsvHeap.GetAddressOf())))) {
			return false;
		}

		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		heapProperties.CreationNodeMask = 1;
		heapProperties.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = static_cast<UINT64>(width);
		desc.Height = static_cast<UINT>(height);
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = graphics.GetRenderTargetFormat();
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		D3D12_CLEAR_VALUE colorClear{};
		colorClear.Format = desc.Format;
		colorClear.Color[0] = 0.1f;
		colorClear.Color[1] = 0.1f;
		colorClear.Color[2] = 0.1f;
		colorClear.Color[3] = 1.0f;

		if (FAILED(device->CreateCommittedResource(
		        &heapProperties, D3D12_HEAP_FLAG_NONE, &desc,
		        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &colorClear,
		        IID_PPV_ARGS(color.GetAddressOf())))) {
			return false;
		}

		device->CreateRenderTargetView(
		    color.Get(), nullptr,
		    rtvHeap->GetCPUDescriptorHandleForHeapStart());

		desc.Format = graphics.GetDepthStencilFormat();
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE depthClear{};
		depthClear.Format = desc.Format;
		depthClear.DepthStencil.Depth = 1.0f;

		if (FAILED(device->CreateCommittedResource(
		        &heapProperties, D3D12_HEAP_FLAG_NONE, &desc,
		        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
		        IID_PPV_ARGS(depth.GetAddressOf())))) {
			return false;
		}

		D3D12_DEPTH_STENCIL_VIEW_DESC depthView{};
		depthView.Format = graphics.GetDepthStencilFormat();
		depthView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

		device->CreateDepthStencilView(
		    depth.Get(), &depthView,
		    dsvHeap->GetCPUDescriptorHandleForHeapStart());

		this->width_ = width;
		this->height_ = height;

		return true;
	}

	// 参照先はRenderTextureより長く保持する
	D3D12Graphics &graphics;

	Microsoft::WRL::ComPtr<ID3D12Resource> color;
	Microsoft::WRL::ComPtr<ID3D12Resource> depth;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap;

	int width_ = 0;
	int height_ = 0;
};

RenderTexture::RenderTexture(D3D12Graphics &graphics)
    : impl_(std::make_unique<Impl>(graphics)) {}

RenderTexture::~RenderTexture() = default;

bool RenderTexture::Resize(int width, int height) {
	if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
		Logger::Error("RenderTexture::Resize: サイズが無効です。");
		return false;
	}

	if (impl_->width_ == width && impl_->height_ == height) {
		return true;
	}

	try {
		// 作成に成功するまで、現在のリソースを保持する
		auto next = std::make_unique<Impl>(impl_->graphics);

		if (!next->Create(width, height)) {
			Logger::Error(std::format(
			    "RenderTexture: リソースの作成に失敗しました。{}x{}", width,
			    height));
			return false;
		}

		impl_ = std::move(next);
		return true;
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "RenderTextureの作成中に例外が発生しました: {}", error.what()));
		return false;
	}
}

int RenderTexture::GetWidth() const { return impl_->width_; }

int RenderTexture::GetHeight() const { return impl_->height_; }

bool RenderTexture::Begin(const float (&clearColor)[4]) {
	auto *commandList = impl_->graphics.GetCommandList();

	if (!commandList || !impl_->color || !impl_->depth) {
		Logger::Error("RenderTexture::Begin: 未初期化です。");
		return false;
	}

	D3D12Helpers::TransitionResource(commandList, impl_->color.Get(),
	                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
	                                 D3D12_RESOURCE_STATE_RENDER_TARGET);

	const auto rtv = impl_->rtvHeap->GetCPUDescriptorHandleForHeapStart();
	const auto dsv = impl_->dsvHeap->GetCPUDescriptorHandleForHeapStart();

	commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
	commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0,
	                                   nullptr);

	commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

	D3D12_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(impl_->width_);
	viewport.Height = static_cast<float>(impl_->height_);
	viewport.MaxDepth = 1.0f;

	D3D12_RECT scissor{};
	scissor.right = static_cast<LONG>(impl_->width_);
	scissor.bottom = static_cast<LONG>(impl_->height_);

	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissor);

	return true;
}

void RenderTexture::End() {
	auto *commandList = impl_->graphics.GetCommandList();

	D3D12Helpers::TransitionResource(
	    commandList, impl_->color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
	    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	impl_->graphics.BindBackBuffer();
}

ID3D12Resource *RenderTexture::GetColorResource() const {
	return impl_->color.Get();
}

bool RenderTexture::Uses(const D3D12Graphics &graphics) const {
	return &impl_->graphics == &graphics;
}
