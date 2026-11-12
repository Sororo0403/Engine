#pragma once

#include "core/Logger.h"
#include "graphics/buffer/ConstantBufferPool.h"

#include <cstring>
#include <type_traits>

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

	ConstantBuffer(const ConstantBuffer &) = delete;
	ConstantBuffer &operator=(const ConstantBuffer &) = delete;
	ConstantBuffer(ConstantBuffer &&) noexcept = default;
	ConstantBuffer &operator=(ConstantBuffer &&) noexcept = default;

	/// <summary>
	/// 型のサイズを256バイト単位に切り上げて確保し、初期値を書き込む
	/// </summary>
	/// <param name="pool">描画基盤の共有定数バッファ</param>
	/// <param name="initialValue">初期値</param>
	/// <returns>true:成功 false:引数が無効、初期化済み、または作成失敗</returns>
	bool Initialize(ConstantBufferPool &pool, const T &initialValue = T{}) {
		if (IsInitialized()) {
			Logger::Error("ConstantBuffer::Initialize: 初期化済みです。");
			return false;
		}

		allocation_ = pool.Allocate(sizeof(T));
		if (!IsInitialized())
			return false;

		std::memcpy(allocation_.GetMappedData(), &initialValue, sizeof(T));
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

		std::memcpy(allocation_.GetMappedData(), &value, sizeof(T));
		return true;
	}

	/// <summary>
	/// 初期化が完了しているか取得する
	/// </summary>
	/// <returns>true:初期化済み false:未初期化</returns>
	bool IsInitialized() const {
		return allocation_.GetMappedData() != nullptr;
	}

	/// <summary>
	/// 定数バッファのGPUアドレスを取得する
	/// </summary>
	/// <returns>GPUアドレス。未初期化の場合は0</returns>
	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const {
		return allocation_.GetGPUVirtualAddress();
	}

	/// <summary>
	/// 領域を解放する。GPUの利用完了後にプールへ戻る
	/// </summary>
	void Finalize() {
		allocation_.Reset();
	}

  private:
	ConstantBufferPool::Allocation allocation_;
};
