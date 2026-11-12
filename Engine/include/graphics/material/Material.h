#pragma once

#include "graphics/buffer/ConstantBuffer.h"
#include "graphics/material/MaterialData.h"

#include <d3d12.h>
#include <memory>

class MaterialResource;

class Material {
  public:
	/// <summary>
	/// 共有する材質を指定し、描画用の定数バッファを作成する
	/// </summary>
	/// <param name="pool">描画基盤の共有定数バッファ</param>
	/// <param name="resource">画像と初期値を持つ材質の共有参照</param>
	/// <returns>true:成功 false:引数が無効、初期化済み、または作成失敗</returns>
	bool Initialize(ConstantBufferPool &pool,
	                std::shared_ptr<const MaterialResource> resource);

	/// <summary>
	/// 材質情報を定数バッファへ書き込む
	/// </summary>
	/// <param name="data">RGBAの各成分が0～1で、UVの倍率と移動量が有限値の材質情報</param>
	/// <returns>true:成功 false:未初期化、またはデータが無効</returns>
	bool Update(const MaterialData &data);

	/// <summary>
	/// 使用するテクスチャのSRVを取得する
	/// </summary>
	/// <returns>GPU側SRVハンドル。未初期化の場合はptrが0</returns>
	D3D12_GPU_DESCRIPTOR_HANDLE GetTextureSrv() const;

	/// <summary>
	/// 材質用の定数バッファのGPUアドレスを取得する
	/// </summary>
	/// <returns>GPUアドレス。未初期化の場合は0</returns>
	D3D12_GPU_VIRTUAL_ADDRESS GetConstantBufferAddress() const;

	/// <summary>
	/// 材質用の定数バッファを解放する。GPUの利用完了後に呼び出す
	/// </summary>
	void Finalize();

	/// <summary>
	/// 定数バッファを保ったまま参照する材質を差し替える。GPU完了後に呼び出す
	/// </summary>
	/// <param name="resource">初期化済みの共有材質</param>
	/// <returns>true:成功 false:未初期化、または材質が無効</returns>
	bool SetResource(std::shared_ptr<const MaterialResource> resource);

	/// <summary>
	/// 参照している共有材質を取得する
	/// </summary>
	/// <returns>材質への共有参照。未初期化の場合は空</returns>
	const std::shared_ptr<const MaterialResource> &GetResource() const;

  private:
	std::shared_ptr<const MaterialResource> resource_;

	ConstantBuffer<MaterialData> constantBuffer_;
};
