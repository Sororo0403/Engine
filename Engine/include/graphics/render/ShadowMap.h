#pragma once
#include "graphics/lighting/ShadowSettings.h"
#include "graphics/buffer/ConstantBuffer.h"
#include <DirectXMath.h>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <wrl/client.h>
class MaterialResource;

/// <summary>
/// 一つの描画ワールドに属する影の深度・光源行列と、材質画像をまとめたSRVヒープ
/// </summary>
class ShadowMap {
  public:
	/// <summary>
	/// 定数バッファと深度用ヒープを作成する
	/// </summary>
	/// <param name="graphics">所有ワールドと同じ描画基盤</param>
	/// <returns>true:成功 false:引数・初期化状態・作成が無効</returns>
	bool Initialize(D3D12Graphics &graphics);

	/// <summary>
	/// GPUの利用完了後、描画命令の記録前に設定・深度・材質の参照を更新する
	/// </summary>
	/// <param name="lightViewProjection">光源からのビュー投影行列</param>
	/// <param name="settings">影の設定。無効時は1画素の深度リソースを用意する</param>
	/// <param name="materials">このワールドで使う材質。重複可、null不可</param>
	/// <returns>true:成功 false:未初期化・設定・リソース作成が無効</returns>
	bool Update(const DirectX::XMFLOAT4X4 &lightViewProjection,
	            const ShadowSettings &settings,
	            std::span<const MaterialResource *const> materials);

	/// <summary>
	/// 深度を消去し、光源からの描画先と範囲を設定する。無効時は何もしない
	/// </summary>
	/// <param name="commands">記録中の同じデバイスのコマンドリスト</param>
	/// <returns>true:深度へ描画する false:影が無効、または未準備</returns>
	bool Begin(ID3D12GraphicsCommandList *commands) const;

	/// <summary>
	/// 深度を読み取り状態へ戻す。呼び出し側は次の描画先と範囲を設定する
	/// </summary>
	/// <param name="commands">Beginを実行したコマンドリスト</param>
	void End(ID3D12GraphicsCommandList *commands) const;

	/// <summary>
	/// 光源行列と補正の定数バッファを取得する
	/// </summary>
	/// <returns>準備済みバッファのGPUアドレス</returns>
	D3D12_GPU_VIRTUAL_ADDRESS GetConstantBufferAddress() const;

	/// <summary>
	/// 影と、このワールドが使用する全材質画像のヒープを取得する
	/// </summary>
	/// <returns>次のUpdateまたは破棄まで有効な借用ポインター</returns>
	ID3D12DescriptorHeap *GetHeap() const;

	/// <summary>
	/// 描画に使用する材質画像のGPU側SRVを取得する
	/// </summary>
	/// <param name="material">Updateで登録した材質</param>
	/// <returns>GPU側の参照。未登録はptrが0</returns>
	D3D12_GPU_DESCRIPTOR_HANDLE
	GetTextureSrv(const MaterialResource *material) const;

	/// <summary>
	/// 同じヒープにある影のSRVを取得する
	/// </summary>
	/// <returns>準備済み深度のGPU側の参照</returns>
	D3D12_GPU_DESCRIPTOR_HANDLE GetShadowSrv() const;

  private:
	struct Data {
		DirectX::XMFLOAT4X4 matrix{};
		float strength = 0, bias = 0, normalBias = 0;
		std::uint32_t enabled = 0;
		float texelSize = 1;
		float padding[3]{};
	};

	ID3D12Device *device_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> depth_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_, views_;
	ConstantBuffer<Data> constants_;
	std::unordered_map<const MaterialResource *, UINT> materials_;
	UINT resolution_ = 0, capacity_ = 0, stride_ = 0;
	bool enabled_ = false;
};
