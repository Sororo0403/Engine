#pragma once

#include "graphics/render/DebugLine.h"
#include "graphics/render/DebugLineVertex.h"

#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <cstddef>
#include <limits>
#include <span>
#include <vector>

/// <summary>
/// ワールドごとの線を、まとめて描画する頂点バッファへ格納する
/// </summary>
class DebugLineBatch {
  public:
	DebugLineBatch() = default;

	// 呼び出し側でGPUの利用完了を保証してから破棄する
	~DebugLineBatch() { Finalize(); }

	DebugLineBatch(const DebugLineBatch &) = delete;
	DebugLineBatch &operator=(const DebugLineBatch &) = delete;

	/// <summary>
	/// 線を頂点へ変換してGPU用バッファを更新する。
	/// GPUの利用完了後、描画命令の記録前に呼び出す
	/// </summary>
	/// <param name="device">バッファの作成に使用する同じD3D12デバイス</param>
	/// <param name="lines">今回描画する線</param>
	/// <returns>true:成功 false:引数が無効、サイズ超過、または作成失敗</returns>
	bool Update(ID3D12Device *device, std::span<const DebugLine> lines);

	/// <summary>
	/// 今回の描画数を戻す。確保済みのGPUバッファは保持する
	/// </summary>
	void Clear();

	/// <summary>
	/// 描画する頂点数を取得する
	/// </summary>
	/// <returns>頂点数。線がない場合は0</returns>
	UINT GetVertexCount() const;

	/// <summary>
	/// 線の頂点バッファビューを取得する
	/// </summary>
	/// <returns>次の更新まで有効な頂点バッファビューへの参照</returns>
	const D3D12_VERTEX_BUFFER_VIEW &GetVertexBufferView() const;

	/// <summary>
	/// GPUバッファを解放する。GPUの利用完了後に呼び出す
	/// </summary>
	void Finalize();

  private:
	static constexpr std::size_t kInitialVertexCapacity = 256;

	static constexpr std::size_t kMaxVertexCount =
	    static_cast<std::size_t>((std::numeric_limits<UINT>::max)()) /
	    sizeof(DebugLineVertex);

	static_assert(kInitialVertexCapacity <= kMaxVertexCount);

	/// <summary>
	/// 必要な頂点数を格納できるGPUバッファを確保する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="requiredCount">必要な頂点数</param>
	/// <returns>true:成功 false:作成またはMap失敗</returns>
	bool EnsureCapacity(ID3D12Device *device, std::size_t requiredCount);

	std::vector<DebugLineVertex> vertices_;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
	void *mappedData_ = nullptr;

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

	std::size_t capacity_ = 0;
	UINT vertexCount_ = 0;
};
