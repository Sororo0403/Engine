#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl/client.h>

class MeshInstance;

class MeshRenderer {
  public:
	/// <summary>
	/// メッシュ描画用のルートシグネチャとPSOを初期化する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="vertexShader">コンパイル済み頂点シェーダー</param>
	/// <param name="pixelShader">コンパイル済みピクセルシェーダー</param>
	/// <returns>true:成功 false:引数が無効、初期化済み、または作成失敗</returns>
	bool Initialize(ID3D12Device *device, IDxcBlob *vertexShader,
	                IDxcBlob *pixelShader);

	/// <summary>
	/// 指定した物体とテクスチャで描画命令を記録する
	/// </summary>
	/// <param name="commandList">テクスチャ用ヒープが設定済みの記録中のコマンドリスト</param>
	/// <param name="instance">初期化・更新済みの物体</param>
	/// <param name="textureSrv">テクスチャのGPU側SRVハンドル</param>
	void Draw(ID3D12GraphicsCommandList *commandList,
	          const MeshInstance &instance,
	          D3D12_GPU_DESCRIPTOR_HANDLE textureSrv);

	/// <summary>
	/// 描画用オブジェクトを解放する。GPUの利用完了後に呼び出す
	/// </summary>
	void Finalize();

  private:
	/// <summary>
	/// シェーダーへのデータの渡し方を定義する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <returns>true:成功 false:失敗</returns>
	bool CreateRootSignature(ID3D12Device *device);

	/// <summary>
	/// メッシュ描画用のパイプラインステートを作成する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="vertexShader">コンパイル済み頂点シェーダー</param>
	/// <param name="pixelShader">コンパイル済みピクセルシェーダー</param>
	/// <returns>true:成功 false:失敗</returns>
	bool CreatePipelineState(ID3D12Device *device, IDxcBlob *vertexShader,
	                         IDxcBlob *pixelShader);

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
};
