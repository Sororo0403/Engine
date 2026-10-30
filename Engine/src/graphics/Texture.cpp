#include "graphics/Texture.h"
#include "core/Logger.h"
#include "platform/windows/StringConversion.h"

#include <directxtk12/ResourceUploadBatch.h>
#include <directxtk12/WICTextureLoader.h>

#include <exception>
#include <format>
#include <utility>

bool Texture::Load(ID3D12Device *device, ID3D12CommandQueue *commandQueue,
                   const std::filesystem::path &filePath) {
	if (!device || !commandQueue || filePath.empty()) {
		Logger::Error("Texture::Load: 引数が無効です。");
		return false;
	}

	if (resource_) {
		Logger::Error("Texture::Load: 読み込み済みです。");
		return false;
	}

	if (commandQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
		Logger::Error("Texture::Load: DIRECT型のコマンドキューが必要です。");
		return false;
	}

	const std::string pathText =
	    StringConversion::WideToUtf8(filePath.wstring());

	try {
		DirectX::ResourceUploadBatch upload(device);
		upload.Begin();

		Microsoft::WRL::ComPtr<ID3D12Resource> texture;

		// 画像を読み込み、GPUへの転送を記録する
		const HRESULT hr = DirectX::CreateWICTextureFromFile(
		    device, upload, filePath.c_str(), texture.GetAddressOf(), true);

		if (FAILED(hr)) {
			Logger::Error(std::format(
			    "テクスチャの読み込みに失敗しました: {} HRESULT=0x{:08X}",
			    pathText, static_cast<unsigned long>(hr)));
			return false;
		}

		// 記録した転送処理を実行する
		auto finished = upload.End(commandQueue);

		// GPUへの転送完了を待ち、非同期処理の例外も受け取る
		finished.get();

		resource_ = std::move(texture);
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "テクスチャの読み込み・転送中に例外が発生しました: {} ({})",
		    pathText, error.what()));
		return false;
	}

	Logger::Info(std::format("テクスチャを読み込みました: {} ({}x{})", pathText,
	                         GetWidth(), GetHeight()));

	return true;
}

bool Texture::LoadFromMemory(ID3D12Device *device,
                             ID3D12CommandQueue *commandQueue,
                             std::span<const std::uint8_t> imageData) {
	if (!device || !commandQueue || imageData.empty() || resource_) {
		Logger::Error(
		    "Texture::LoadFromMemory: 引数が無効、または読み込み済みです。");
		return false;
	}

	if (commandQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
		Logger::Error(
		    "Texture::LoadFromMemory: DIRECT型のコマンドキューが必要です。");
		return false;
	}

	try {
		DirectX::ResourceUploadBatch upload(device);
		upload.Begin();

		Microsoft::WRL::ComPtr<ID3D12Resource> texture;

		const HRESULT hr = DirectX::CreateWICTextureFromMemory(
		    device, upload, imageData.data(), imageData.size(),
		    texture.GetAddressOf(), true);

		if (FAILED(hr)) {
			Logger::Error(std::format(
			    "メモリ上の画像の読み込みに失敗しました。HRESULT=0x{:08X}",
			    static_cast<unsigned long>(hr)));
			return false;
		}

		auto finished = upload.End(commandQueue);
		finished.get();

		resource_ = std::move(texture);
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "メモリ上の画像の読み込み・転送に失敗しました: {}", error.what()));
		return false;
	}

	Logger::Info(std::format("メモリ上の画像を読み込みました。{}x{}",
	                         GetWidth(), GetHeight()));

	return true;
}

bool Texture::CreateFromRGBA(ID3D12Device *device,
                             ID3D12CommandQueue *commandQueue, UINT width,
                             UINT height,
                             std::span<const std::uint8_t> pixels) {
	if (!device || !commandQueue || resource_ || width == 0 || height == 0 ||
	    width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
	    height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
		Logger::Error(
		    "Texture::CreateFromRGBA: 引数が無効、または読み込み済みです。");
		return false;
	}

	const std::size_t requiredSize =
	    static_cast<std::size_t>(width) * height * 4;

	if (pixels.size() != requiredSize) {
		Logger::Error(
		    "Texture::CreateFromRGBA: 画素データのサイズが一致しません。");
		return false;
	}

	if (commandQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
		Logger::Error(
		    "Texture::CreateFromRGBA: DIRECT型のコマンドキューが必要です。");
		return false;
	}

	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	heap.CreationNodeMask = 1;
	heap.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = width;
	desc.Height = height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;

	Microsoft::WRL::ComPtr<ID3D12Resource> texture;

	const HRESULT hr = device->CreateCommittedResource(
	    &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
	    nullptr, IID_PPV_ARGS(texture.GetAddressOf()));

	if (FAILED(hr)) {
		Logger::Error(
		    std::format("RGBAテクスチャの作成に失敗しました。HRESULT=0x{:08X}",
		                static_cast<unsigned long>(hr)));
		return false;
	}

	try {
		DirectX::ResourceUploadBatch upload(device);
		upload.Begin();

		D3D12_SUBRESOURCE_DATA data{};
		data.pData = pixels.data();
		data.RowPitch = static_cast<LONG_PTR>(width) * 4;
		data.SlicePitch = data.RowPitch * height;

		upload.Upload(texture.Get(), 0, &data, 1);

		upload.Transition(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
		                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		auto finished = upload.End(commandQueue);
		finished.get();

		resource_ = std::move(texture);
	} catch (const std::exception &error) {
		Logger::Error(std::format("RGBAテクスチャの転送に失敗しました: {}",
		                          error.what()));
		return false;
	}

	Logger::Info(std::format("RGBAテクスチャを作成しました。{}x{}", GetWidth(),
	                         GetHeight()));

	return true;
}

void Texture::Finalize() { resource_.Reset(); }

ID3D12Resource *Texture::GetResource() const { return resource_.Get(); }

UINT Texture::GetWidth() const {
	if (!resource_) {
		return 0;
	}

	return static_cast<UINT>(resource_->GetDesc().Width);
}

UINT Texture::GetHeight() const {
	if (!resource_) {
		return 0;
	}

	return resource_->GetDesc().Height;
}
