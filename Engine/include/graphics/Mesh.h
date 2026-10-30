#pragma once

#include "graphics/Vertex.h"

#include <cstdint>
#include <d3d12.h>
#include <span>
#include <wrl/client.h>

class Mesh {
  public:
	/// <summary>
	/// 三角形リスト用の頂点・インデックスバッファを作成する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="vertices">頂点データ</param>
	/// <param name="indices">頂点番号。要素数は3の倍数</param>
	/// <returns>true:成功 false:引数が無効、初期化済み、または作成失敗</returns>
	bool Initialize(ID3D12Device *device, std::span<const Vertex> vertices,
	                std::span<const std::uint32_t> indices);

	/// <summary>
	/// 頂点・インデックスバッファを設定し、描画命令を記録する
	/// </summary>
	/// <param name="commandList">シェーダーなどが設定済みの記録中のコマンドリスト</param>
	void Draw(ID3D12GraphicsCommandList *commandList) const;

	/// <summary>
	/// バッファを解放する。GPUの利用完了後に呼び出す
	/// </summary>
	void Finalize();

  private:
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

	UINT indexCount_ = 0;
};
