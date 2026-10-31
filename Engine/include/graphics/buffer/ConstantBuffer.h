#pragma once

#include "core/Logger.h"

#include <directxtk12/BufferHelpers.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstring>
#include <format>
#include <type_traits>
#include <utility>

/// <summary>
/// GPUへ渡す値を保持する、常時Mapされた定数バッファ
/// </summary>
/// <typeparam name="T">シェーダー側の配置に合わせた、単純コピー可能な標準レイアウト型</typeparam>
template <typename T> class ConstantBuffer {
	static_assert(std::is_trivially_copyable_v<T> &&
	              std::is_standard_layout_v<T>);
	static_assert(sizeof(T) <= D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16);

  public:
	ConstantBuffer() = default;

	// 解放前にGPUの利用を完了させること
	~ConstantBuffer() { Finalize(); }

	ConstantBuffer(const ConstantBuffer &) = delete;
	ConstantBuffer &operator=(const ConstantBuffer &) = delete;

	ConstantBuffer(ConstantBuffer &&other) noexcept
	    : resource_(std::move(other.resource_)),
	      mappedData_(std::exchange(other.mappedData_, nullptr)) {}

	/// <summary>
	/// 所有権を移動する。移動先のバッファはGPUの利用完了後に置き換える
	/// </summary>
	/// <param name="other">移動元。移動後は未初期化になる</param>
	/// <returns>このバッファへの参照</returns>
	ConstantBuffer &operator=(ConstantBuffer &&other) noexcept {
		if (this != &other) {
			Finalize();
			resource_ = std::move(other.resource_);
			mappedData_ = std::exchange(other.mappedData_, nullptr);
		}
		return *this;
	}

	/// <summary>
	/// 型のサイズを256バイト単位に切り上げて確保し、初期値を書き込む
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="initialValue">初期値</param>
	/// <returns>true:成功 false:引数が無効、初期化済み、または作成失敗</returns>
	bool Initialize(ID3D12Device *device, const T &initialValue = T{}) {
		if (!device || resource_) {
			Logger::Error("ConstantBuffer::Initialize: "
			              "引数が無効、または初期化済みです。");
			return false;
		}

		constexpr std::size_t alignment =
		    D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
		constexpr std::size_t size =
		    (sizeof(T) + alignment - 1) / alignment * alignment;
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		HRESULT hr = DirectX::CreateUploadBuffer(device, nullptr, 1, size,
		                                         resource.GetAddressOf());
		if (FAILED(hr)) {
			Logger::Error(std::format(
			    "定数バッファの作成に失敗しました。HRESULT=0x{:08X}",
			    static_cast<unsigned long>(hr)));
			return false;
		}

		void *mapped = nullptr;
		const D3D12_RANGE readRange{0, 0};
		hr = resource->Map(0, &readRange, &mapped);
		if (FAILED(hr)) {
			Logger::Error(
			    std::format("定数バッファのMapに失敗しました。HRESULT=0x{:08X}",
			                static_cast<unsigned long>(hr)));
			return false;
		}

		resource_ = std::move(resource);
		mappedData_ = mapped;
		std::memcpy(mappedData_, &initialValue, sizeof(T));
		return true;
	}

	/// <summary>
	/// 定数バッファへ値を書き込む。前回のGPU利用完了後に呼び出す
	/// </summary>
	/// <param name="value">書き込む値。値の検証は呼び出し元で行う</param>
	/// <returns>true:成功 false:未初期化</returns>
	bool Update(const T &value) {
		if (!IsInitialized()) {
			Logger::Error("ConstantBuffer::Update: 未初期化です。");
			return false;
		}
		std::memcpy(mappedData_, &value, sizeof(T));
		return true;
	}

	/// <summary>
	/// 初期化が完了しているか取得する
	/// </summary>
	/// <returns>true:初期化済み false:未初期化</returns>
	bool IsInitialized() const { return mappedData_ != nullptr; }

	/// <summary>
	/// 定数バッファのGPUアドレスを取得する
	/// </summary>
	/// <returns>GPUアドレス。未初期化の場合は0</returns>
	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const {
		return resource_ ? resource_->GetGPUVirtualAddress() : 0;
	}

	/// <summary>
	/// Mapを解除して解放する。GPUの利用完了後に呼び出す
	/// </summary>
	void Finalize() {
		if (resource_ && mappedData_) {
			resource_->Unmap(0, nullptr);
		}
		mappedData_ = nullptr;
		resource_.Reset();
	}

  private:
	Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
	void *mappedData_ = nullptr;
};
