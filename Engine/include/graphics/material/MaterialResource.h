#pragma once

#include "graphics/material/MaterialData.h"
#include "graphics/material/Texture.h"
#include "graphics/material/TextureData.h"
#include <directxtk12/DescriptorHeap.h>
#include <memory>

/// <summary>
/// メッシュとは独立した、共有する材質の初期値・画像・SRVを保持する
/// </summary>
class MaterialResource {
  public:
	/// <summary>
	/// 材質用の画像とSRVを作成する。GPU転送の完了まで待機する
	/// </summary>
	/// <param name="device">作成先のデバイス</param>
	/// <param name="queue">同じデバイスのDIRECTキュー</param>
	/// <param name="data">材質パラメーター</param>
	/// <param name="texture">読み込む画像。NONEは白い画像を作成する</param>
	/// <returns>true:成功 false:引数が無効、初期化済み、または作成失敗</returns>
	bool Initialize(ID3D12Device *device, ID3D12CommandQueue *queue,
	                const MaterialData &data, const TextureData &texture);

	/// <summary>
	/// 共有する材質の初期値を取得する
	/// </summary>
	/// <returns>読み取り専用の材質情報</returns>
	const MaterialData &GetData() const;

	/// <summary>
	/// この材質の画像を格納したヒープを取得する
	/// </summary>
	/// <returns>借用ポインター。未初期化時はnullptr</returns>
	ID3D12DescriptorHeap *GetDescriptorHeap() const;

	/// <summary>
	/// この材質の画像のSRVを取得する
	/// </summary>
	/// <returns>GPU側SRV。未初期化時はptrが0</returns>
	D3D12_GPU_DESCRIPTOR_HANDLE GetTextureSrv() const;

	/// <summary>
	/// 共有する画像のSRVを指定したCPU側スロットへ作成する。画像の所有権は移さない
	/// </summary>
	/// <param name="device">材質を作成した同じデバイス</param>
	/// <param name="destination">呼び出し側が確保したCBV/SRV/UAVヒープのCPU側の参照</param>
	/// <returns>true:作成成功 false:デバイス・画像・参照が無効</returns>
	bool CreateTextureView(ID3D12Device *device,
	                       D3D12_CPU_DESCRIPTOR_HANDLE destination) const;

  private:
	MaterialData data_;
	Texture texture_;
	std::unique_ptr<DirectX::DescriptorHeap> heap_;
};
