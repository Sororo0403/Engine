#pragma once

#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl/client.h>

/// <summary>
/// GPUへ渡す材質の情報
/// </summary>
struct MaterialData {
	// テクスチャの色に掛けるRGBA
	DirectX::XMFLOAT4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};

	// UV座標に掛ける倍率
	DirectX::XMFLOAT2 uvScale{1.0f, 1.0f};

	// UV座標に加える移動量
	DirectX::XMFLOAT2 uvOffset{0.0f, 0.0f};
};

class Material {
  public:
	/// <summary>
	/// 使用するテクスチャを指定し、材質用の定数バッファを作成する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="textureSrv">テクスチャのGPU側SRVハンドル。画像とヒープは別途保持すること</param>
	/// <returns>true:成功 false:引数が無効、初期化済み、または作成失敗</returns>
	bool Initialize(ID3D12Device *device,
	                D3D12_GPU_DESCRIPTOR_HANDLE textureSrv);

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

  private:
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrv_{};

	Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
	MaterialData *mappedData_ = nullptr;
};
