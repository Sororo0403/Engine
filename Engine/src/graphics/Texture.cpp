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
