#pragma once

#include "graphics/DirectionalLightData.h"
#include "graphics/buffer/ConstantBuffer.h"

#include <d3d12.h>

class DirectionalLight {
  public:
	/// <summary>
	/// 光源用の定数バッファを作成する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <returns>true:成功 false:引数が無効、初期化済み、または作成失敗</returns>
	bool Initialize(ID3D12Device *device);

	/// <summary>
	/// 光源情報を定数バッファへ書き込む
	/// </summary>
	/// <param name="data">光が進む方向・強さ・色・環境光の強さ</param>
	/// <returns>true:成功 false:未初期化、またはデータが無効</returns>
	bool Update(const DirectionalLightData &data);

	/// <summary>
	/// 定数バッファのGPUアドレスを取得する
	/// </summary>
	/// <returns>GPUアドレス。未初期化の場合は0</returns>
	D3D12_GPU_VIRTUAL_ADDRESS GetConstantBufferAddress() const;

	/// <summary>
	/// 定数バッファを解放する。GPUの利用完了後に呼び出す
	/// </summary>
	void Finalize();

  private:
	ConstantBuffer<DirectionalLightData> constantBuffer_;
};
