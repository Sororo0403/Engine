#pragma once

#include "graphics/Transform.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl/client.h>

class TriangleRenderer {
  public:
	/// <summary>
	/// 三角形の描画に必要なオブジェクトを初期化する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="vertexShader">コンパイル済み頂点シェーダー</param>
	/// <param name="pixelShader">コンパイル済みピクセルシェーダー</param>
	/// <returns>true:成功 false:失敗</returns>
	bool Initialize(ID3D12Device *device, IDxcBlob *vertexShader,
	                IDxcBlob *pixelShader);

	/// <summary>
	/// 変形情報から行列を作成し、定数バッファを更新する
	/// </summary>
	void Update();

	/// <summary>
	/// 三角形の描画命令を記録する
	/// </summary>
	/// <param name="commandList">記録中のコマンドリスト</param>
	void Draw(ID3D12GraphicsCommandList *commandList);

	/// <summary>
	/// 描画用オブジェクトを解放する
	/// </summary>
	void Finalize();

	/// <summary>
	/// 三角形の変形情報を設定する
	/// </summary>
	/// <param name="transform">拡大率・回転角・移動量</param>
	void SetTransform(const Transform &transform);

  private:
	/// <summary>
	/// シェーダーへのデータの渡し方を定義する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <returns>true:成功 false:失敗</returns>
	bool CreateRootSignature(ID3D12Device *device);

	/// <summary>
	/// 三角形描画用のパイプラインステートを作成する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="vertexShader">コンパイル済み頂点シェーダー</param>
	/// <param name="pixelShader">コンパイル済みピクセルシェーダー</param>
	/// <returns>true:成功 false:失敗</returns>
	bool CreatePipelineState(ID3D12Device *device, IDxcBlob *vertexShader,
	                         IDxcBlob *pixelShader);

	/// <summary>
	/// 三角形の頂点データを格納するバッファを作成する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <returns>true:成功 false:失敗</returns>
	bool CreateVertexBuffer(ID3D12Device *device);

	/// <summary>
	/// 変換行列を格納する定数バッファを作成する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <returns>true:成功 false:失敗</returns>
	bool CreateConstantBuffer(ID3D12Device *device);

  private:
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

	Transform transform_{};
	Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
	DirectX::XMFLOAT4X4 *mappedWorld_ = nullptr;
};
