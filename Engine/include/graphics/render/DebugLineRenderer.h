#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include <DirectXMath.h>
#include <cstdint>

class DebugLineBatch;

/// <summary>
/// ワールド座標の線をまとめて描画する
/// </summary>
class DebugLineRenderer {
  public:
	DebugLineRenderer() = default;

	DebugLineRenderer(const DebugLineRenderer &) = delete;
	DebugLineRenderer &operator=(const DebugLineRenderer &) = delete;

	/// <summary>
	/// 線描画用のルートシグネチャとPSOを初期化する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="vertexShader">コンパイル済み頂点シェーダー</param>
	/// <param name="pixelShader">コンパイル済みピクセルシェーダー</param>
	/// <param name="renderTargetFormat">描画先のカラー形式</param>
	/// <param name="depthStencilFormat">描画先の深度形式</param>
	/// <returns>true:成功 false:引数が無効、初期化済み、または作成失敗</returns>
	bool Initialize(ID3D12Device *device, IDxcBlob *vertexShader,
	                IDxcBlob *pixelShader, DXGI_FORMAT renderTargetFormat,
	                DXGI_FORMAT depthStencilFormat);

	/// <summary>
	/// 更新済みの線を1回の描画命令で描画する
	/// </summary>
	/// <param name="commandList">描画先とビューポートが設定済みのコマンドリスト</param>
	/// <param name="batch">今回描画する更新済みの頂点バッファ</param>
	/// <param name="view">ビュー行列</param>
	/// <param name="projection">投影行列</param>
	void Draw(ID3D12GraphicsCommandList *commandList,
	          const DebugLineBatch &batch, const DirectX::XMFLOAT4X4 &view,
	          const DirectX::XMFLOAT4X4 &projection) const;

	/// <summary>
	/// 描画用オブジェクトを解放する。GPUの利用完了後に呼び出す
	/// </summary>
	void Finalize();

  private:
	/// <summary>
	/// シェーダーへルート定数で渡すカメラ行列
	/// </summary>
	struct Constants {
		DirectX::XMFLOAT4X4 viewProjection{};
	};

	static constexpr UINT kConstantCount =
	    static_cast<UINT>(sizeof(Constants) / sizeof(std::uint32_t));

	static_assert(kConstantCount == 16);

	/// <summary>
	/// カメラ行列を渡すルートシグネチャを作成する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <returns>true:成功 false:作成失敗</returns>
	bool CreateRootSignature(ID3D12Device *device);

	/// <summary>
	/// 深度を書き込まずに線を描画するPSOを作成する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="vertexShader">コンパイル済み頂点シェーダー</param>
	/// <param name="pixelShader">コンパイル済みピクセルシェーダー</param>
	/// <param name="renderTargetFormat">描画先のカラー形式</param>
	/// <param name="depthStencilFormat">描画先の深度形式</param>
	/// <returns>true:成功 false:作成失敗</returns>
	bool CreatePipelineState(ID3D12Device *device, IDxcBlob *vertexShader,
	                         IDxcBlob *pixelShader,
	                         DXGI_FORMAT renderTargetFormat,
	                         DXGI_FORMAT depthStencilFormat);

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
};
