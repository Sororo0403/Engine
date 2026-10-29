#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <dxcapi.h>

class TriangleRenderer {
public:
	/// <summary>
	/// 三角形の描画に必要なオブジェクトを初期化する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="vertexShader">コンパイル済み頂点シェーダー</param>
	/// <param name="pixelShader">コンパイル済みピクセルシェーダー</param>
	/// <returns>true:成功 false:失敗</returns>
	bool Initialize(ID3D12Device *device, IDxcBlob *vertexShader, IDxcBlob *pixelShader);

	/// <summary>
	/// 三角形の描画命令を記録する
	/// </summary>
	/// <param name="commandList">記録中のコマンドリスト</param>
	void Draw(ID3D12GraphicsCommandList *commandList);

	/// <summary>
	/// 描画用オブジェクトを解放する
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
	/// 三角形描画用のパイプラインステートを作成する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="vertexShader">コンパイル済み頂点シェーダー</param>
	/// <param name="pixelShader">コンパイル済みピクセルシェーダー</param>
	/// <returns>true:成功 false:失敗</returns>
	bool CreatePipelineState(
		ID3D12Device *device,
		IDxcBlob *vertexShader,
		IDxcBlob *pixelShader
	);

private:
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
};
