#pragma once

#include "graphics/Transform.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl/client.h>

class Camera;
class Mesh;

class MeshRenderer {
  public:
	/// <summary>
	/// メッシュ描画用のシェーダー設定と定数バッファを初期化する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="vertexShader">コンパイル済み頂点シェーダー</param>
	/// <param name="pixelShader">コンパイル済みピクセルシェーダー</param>
	/// <returns>true:成功 false:引数が無効、初期化済み、または作成失敗</returns>
	bool Initialize(ID3D12Device *device, IDxcBlob *vertexShader,
	                IDxcBlob *pixelShader);

	/// <summary>
	/// カメラを反映した変換行列を定数バッファへ書き込む
	/// </summary>
	/// <param name="camera">更新済みのカメラ</param>
	void Update(const Camera &camera);

	/// <summary>
	/// 指定したメッシュとテクスチャで描画命令を記録する
	/// </summary>
	/// <param name="commandList">テクスチャ用ヒープが設定済みの記録中のコマンドリスト</param>
	/// <param name="mesh">初期化済みの描画するメッシュ</param>
	/// <param name="textureSrv">テクスチャのGPU側SRVハンドル</param>
	void Draw(ID3D12GraphicsCommandList *commandList, const Mesh &mesh,
	          D3D12_GPU_DESCRIPTOR_HANDLE textureSrv);

	/// <summary>
	/// 描画用オブジェクトを解放する。GPUの利用完了後に呼び出す
	/// </summary>
	void Finalize();

	/// <summary>
	/// 描画する物体の変形情報を設定する
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
	/// メッシュ描画用のパイプラインステートを作成する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="vertexShader">コンパイル済み頂点シェーダー</param>
	/// <param name="pixelShader">コンパイル済みピクセルシェーダー</param>
	/// <returns>true:成功 false:失敗</returns>
	bool CreatePipelineState(ID3D12Device *device, IDxcBlob *vertexShader,
	                         IDxcBlob *pixelShader);

	/// <summary>
	/// 変換行列を格納する定数バッファを作成する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <returns>true:成功 false:失敗</returns>
	bool CreateConstantBuffer(ID3D12Device *device);

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

	Transform transform_{};
	Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
	DirectX::XMFLOAT4X4 *mappedWorld_ = nullptr;
};
