#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl/client.h>

class MeshInstance;
class DirectionalLight;
class PointLights;
class SpotLights;
class ShadowMap;

class MeshRenderer {
  public:
	/// <summary>
	/// メッシュ描画用のルートシグネチャとPSOを初期化する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="vertexShader">コンパイル済み頂点シェーダー</param>
	/// <param name="pixelShader">コンパイル済みピクセルシェーダー</param>
	/// <param name="renderTargetFormat">描画先のカラー形式</param>
	/// <param name="depthStencilFormat">描画先の深度形式</param>
	/// <param name="shadowVertexShader">光源から深度を描画する頂点シェーダー</param>
	/// <returns>true:成功 false:引数が無効、初期化済み、または作成失敗</returns>
	bool Initialize(ID3D12Device *device, IDxcBlob *vertexShader,
	                IDxcBlob *pixelShader, DXGI_FORMAT renderTargetFormat,
	                DXGI_FORMAT depthStencilFormat,
	                IDxcBlob *shadowVertexShader);

	/// <summary>
	/// 物体が指定するメッシュと材質を使い、平行光源を反映して描画する
	/// </summary>
	/// <param name="commandList">材質のテクスチャ用ヒープが設定済みの記録中のコマンドリスト</param>
	/// <param name="instance">初期化・更新済みの物体</param>
	/// <param name="pointLights">更新済みの点光源配列</param>
	/// <param name="shadows">このワールド専用の影と材質画像の参照</param>
	/// <param name="spotLights">更新済みのスポットライト配列</param>
	/// <param name="light">初期化・更新済みの平行光源</param>
	void Draw(ID3D12GraphicsCommandList *commandList,
	          const MeshInstance &instance, const DirectionalLight &light,
	          const PointLights &pointLights, const SpotLights &spotLights,
	          const ShadowMap &shadows);

	/// <summary>
	/// 描画用オブジェクトを解放する。GPUの利用完了後に呼び出す
	/// </summary>

	void Finalize();

	/// <summary>
	/// 指定したメッシュを、影の深度だけへ描画する
	/// </summary>
	/// <param name="commands">影の描画先を設定したコマンドリスト</param>
	/// <param name="instance">更新済みの物体行列と形状</param>
	/// <param name="shadows">更新済みの光源行列</param>
	void DrawShadow(ID3D12GraphicsCommandList *commands,
	                const MeshInstance &instance, const ShadowMap &shadows);

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
	/// <param name="renderTargetFormat">描画先のカラー形式</param>
	/// <param name="depthStencilFormat">描画先の深度形式</param>
	/// <returns>true:成功 false:失敗</returns>
	bool CreatePipelineState(ID3D12Device *device, IDxcBlob *vertexShader,
	                         IDxcBlob *pixelShader,
	                         DXGI_FORMAT renderTargetFormat,
	                         DXGI_FORMAT depthStencilFormat);

	/// <summary>
	/// 光源からの深度専用パイプラインを作成する
	/// </summary>
	/// <param name="device">同じ描画デバイス</param>
	/// <param name="shader">深度用の頂点シェーダー</param>
	/// <returns>true:成功 false:作成失敗</returns>
	bool CreateShadowPipeline(ID3D12Device *device, IDxcBlob *shader);
	Microsoft::WRL::ComPtr<ID3D12RootSignature> shadowRoot_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPipeline_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
};
