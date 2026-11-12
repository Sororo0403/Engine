#pragma once

#include <d3d12.h>

#include <cstddef>
#include <memory>
#include <vector>

class D3D12Graphics;

/// <summary>
/// 大きなアップロードバッファを分割して、定数バッファ間で共有する
/// </summary>
/// <remarks>描画スレッドで使う。解放した領域はフレームのGPU完了後に再利用する</remarks>
class ConstantBufferPool {
	struct Page;

  public:
	ConstantBufferPool() = default;
	ConstantBufferPool(const ConstantBufferPool &) = delete;
	ConstantBufferPool &operator=(const ConstantBufferPool &) = delete;

	/// <summary>
	/// 定数バッファ用の領域を保持する。破棄時は領域を再利用待ちにする
	/// </summary>
	class Allocation {
	  public:
		Allocation() = default;
		~Allocation();
		Allocation(Allocation &&other) noexcept = default;
		Allocation &operator=(Allocation &&other) noexcept;

		void *GetMappedData() const noexcept;
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const noexcept;
		void Reset() noexcept;

	  private:
		friend class ConstantBufferPool;
		Allocation(std::shared_ptr<Page> page, std::size_t slot) noexcept;

		std::shared_ptr<Page> page_;
		std::size_t slot_ = 0;
	};

	/// <summary>
	/// 256バイト境界にそろえた定数バッファ領域を確保する
	/// </summary>
	/// <param name="bytes">書き込むデータのバイト数</param>
	/// <returns>確保した領域。失敗時は空</returns>
	Allocation Allocate(std::size_t bytes);

  private:
	friend class D3D12Graphics;
	void ReclaimCompletedFrame() noexcept;
	void Finalize() noexcept;

	ID3D12Device *device_ = nullptr;
	std::vector<std::shared_ptr<Page>> pages_;
};
