#pragma once

#include "graphics/material/MaterialResource.h"
#include "graphics/mesh/Mesh.h"

#include <DirectXCollision.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct ModelData;
struct Ray;

/// <summary>
/// モデルの描画に使用する共有リソースを保持する
/// </summary>
class Model {
  private:
	/// <summary>
	/// メッシュのGPUリソース・材質番号・選択判定用の形状
	/// </summary>
	struct MeshPart {
		Mesh mesh;
		std::size_t materialIndex = 0;

		std::vector<DirectX::XMFLOAT3> positions;
		std::vector<std::uint32_t> indices;

		// モデルのローカル座標で表す外接ボックス
		DirectX::BoundingBox bounds;
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
	/// 元モデルの材質名を取得する
	/// </summary>
	/// <param name="materialIndex">モデル内の材質スロット番号</param>
	/// <returns>材質名。未設定・範囲外は空。モデルの生存中は参照が有効</returns>
	std::string_view GetMaterialName(std::size_t materialIndex) const;

	/// <summary>
	/// 指定した材質スロットを使用するメッシュ数を取得する
	/// </summary>
	/// <param name="materialIndex">モデル内の材質スロット番号</param>
	/// <returns>使用するメッシュ数。未使用・範囲外は0</returns>
	std::size_t GetMaterialMeshCount(std::size_t materialIndex) const;

	/// <summary>
	/// モデルに設定されている材質の初期値を取得する
	/// </summary>
	/// <param name="materialIndex">0以上、GetMaterialCount()未満の材質番号</param>
	/// <returns>材質の初期値への読み取り専用参照</returns>
	const MaterialData &GetDefaultMaterialData(std::size_t materialIndex) const;

	/// <summary>
	/// 読み込み時の既定材質への共有参照を取得する
	/// </summary>
	/// <param name="materialIndex">モデル内の材質スロット番号</param>
	/// <returns>メッシュとは独立した既定材質の共有参照</returns>
	const std::shared_ptr<const MaterialResource> &
	GetDefaultMaterial(std::size_t materialIndex) const;

	/// <summary>
	/// モデルのローカル座標で、レイと三角形の交差を判定する
	/// </summary>
	/// <param name="ray">ローカル座標のレイ。方向は長さ1</param>
	/// <param name="distance">交差した場合に設定する、始点からの距離</param>
	/// <returns>true:交差あり false:交差なし、またはレイが無効</returns>
	bool Raycast(const Ray &ray, float &distance) const;

	/// <summary>
	/// モデル全体を囲むボックスの頂点をローカル座標で取得する
	/// </summary>
	/// <param name="corners">成功した場合に設定する8頂点</param>
	/// <returns>true:成功 false:メッシュなし</returns>
	bool GetBoundsCorners(std::array<DirectX::XMFLOAT3, 8> &corners) const;

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

  private:
	// 全メッシュを囲む、モデルのローカル座標のボックス
	DirectX::BoundingBox bounds_{};

	std::vector<MeshPart> parts_;

	// 材質スロットの元の名前と、参照するメッシュの数
	std::vector<std::string> materialNames_;
	std::vector<std::size_t> materialMeshCounts_;

	// モデルから読み込んだ既定材質への参照。画像とSRVは材質側が保持する
	std::vector<std::shared_ptr<const MaterialResource>> defaultMaterials_;
};
