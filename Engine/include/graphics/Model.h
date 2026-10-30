#pragma once

#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Texture.h"

#include <directxtk12/DescriptorHeap.h>

#include <cstddef>
#include <memory>
#include <vector>

struct ModelData;

/// <summary>
/// モデルの描画に使用する共有リソースを保持する
/// </summary>
class Model {
  private:
	/// <summary>
	/// GPU側のメッシュと、そのメッシュが使用する材質番号
	/// </summary>
	struct MeshPart {
		Mesh mesh;
		std::size_t materialIndex = 0;
	};

  public:
	/// <summary>
	/// 読み込み済みのモデルデータからGPUリソースを作成する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="commandQueue">同じデバイスのDIRECT型コマンドキュー</param>
	/// <param name="data">読み込み済みのモデルデータ。初期化後は破棄してよい</param>
	/// <returns>true:成功 false:引数が無効、初期化済み、または作成失敗</returns>
	bool Initialize(ID3D12Device *device, ID3D12CommandQueue *commandQueue,
	                const ModelData &data);

	/// <summary>
	/// メッシュ数を取得する
	/// </summary>
	/// <returns>メッシュ数。未初期化の場合は0</returns>
	std::size_t GetMeshCount() const;

	/// <summary>
	/// 指定したメッシュを取得する
	/// </summary>
	/// <param name="index">0以上、GetMeshCount()未満のメッシュ番号</param>
	/// <returns>モデルが保持するメッシュへの読み取り専用参照</returns>
	const Mesh &GetMesh(std::size_t index) const;

	/// <summary>
	/// メッシュが使用する材質番号を取得する
	/// </summary>
	/// <param name="meshIndex">0以上、GetMeshCount()未満のメッシュ番号</param>
	/// <returns>モデル内の材質番号</returns>
	std::size_t GetMeshMaterialIndex(std::size_t meshIndex) const;

	/// <summary>
	/// 材質数を取得する
	/// </summary>
	/// <returns>材質数。未初期化の場合は0</returns>
	std::size_t GetMaterialCount() const;

	/// <summary>
	/// モデルに設定されている材質の初期値を取得する
	/// </summary>
	/// <param name="materialIndex">0以上、GetMaterialCount()未満の材質番号</param>
	/// <returns>材質の初期値への読み取り専用参照</returns>
	const MaterialData &GetDefaultMaterialData(std::size_t materialIndex) const;

	/// <summary>
	/// 材質が使用するテクスチャのSRVを取得する
	/// </summary>
	/// <param name="materialIndex">0以上、GetMaterialCount()未満の材質番号</param>
	/// <returns>モデルが保持するヒープ内のGPU側SRVハンドル</returns>
	D3D12_GPU_DESCRIPTOR_HANDLE GetTextureSrv(std::size_t materialIndex) const;

	/// <summary>
	/// テクスチャのSRVを格納したヒープを取得する
	/// </summary>
	/// <returns>ヒープへの借用ポインタ。未初期化の場合はnullptr</returns>
	ID3D12DescriptorHeap *GetDescriptorHeap() const;

	/// <summary>
	/// GPUリソースを解放する。GPUの利用完了後、参照する物体より後に呼び出す
	/// </summary>
	void Finalize();

  private:
	/// <summary>
	/// 各メッシュのGPUバッファを作成する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="data">読み込み済みのモデルデータ</param>
	/// <returns>true:成功 false:材質番号が無効、または作成失敗</returns>
	bool CreateMeshes(ID3D12Device *device, const ModelData &data);

	/// <summary>
	/// 白いテクスチャと、各材質のテクスチャを作成する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="commandQueue">同じデバイスのDIRECT型コマンドキュー</param>
	/// <param name="data">読み込み済みのモデルデータ</param>
	/// <returns>true:成功 false:読み込みまたは作成失敗</returns>
	bool CreateTextures(ID3D12Device *device, ID3D12CommandQueue *commandQueue,
	                    const ModelData &data);

	/// <summary>
	/// SRVヒープと、各材質のテクスチャを参照するSRVを作成する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <returns>true:成功 false:作成失敗。例外はInitializeで処理する</returns>
	bool CreateShaderResourceViews(ID3D12Device *device);

  private:
	std::vector<MeshPart> parts_;
	std::vector<Texture> textures_;

	// 物体ごとの材質を作るときにコピーする初期値
	std::vector<MaterialData> defaultMaterials_;

	std::unique_ptr<DirectX::DescriptorHeap> srvHeap_;
};
