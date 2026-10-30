#pragma once

#include "graphics/Material.h"
#include "graphics/MeshInstance.h"
#include "graphics/Transform.h"

#include <cstddef>
#include <vector>

class Camera;
class DirectionalLight;
class MeshRenderer;
class Model;

/// <summary>
/// 共有モデルを使用する、1つの物体の描画情報を保持する
/// </summary>
class ModelInstance {
  public:
	ModelInstance() = default;

	// GPUリソースを持つ物体のコピーを禁止する
	ModelInstance(const ModelInstance &) = delete;
	ModelInstance &operator=(const ModelInstance &) = delete;

	/// <summary>
	/// モデルを参照し、物体ごとの材質と描画インスタンスを作成する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="model">初期化済みのモデル。この物体より長く保持すること</param>
	/// <returns>true:成功 false:引数が無効、初期化済み、または作成失敗</returns>
	bool Initialize(ID3D12Device *device, const Model &model);

	/// <summary>
	/// 各メッシュの変換行列と、材質の定数バッファを更新する
	/// </summary>
	/// <param name="camera">更新済みのカメラ</param>
	/// <returns>true:成功 false:未初期化、または材質更新失敗</returns>
	bool Update(const Camera &camera);

	/// <summary>
	/// モデルのSRVヒープを設定し、各メッシュの描画命令を記録する
	/// </summary>
	/// <param name="commandList">記録中のコマンドリスト</param>
	/// <param name="renderer">初期化済みのメッシュ描画クラス</param>
	/// <param name="light">更新済みの平行光源</param>
	void Draw(ID3D12GraphicsCommandList *commandList, MeshRenderer &renderer,
	          const DirectionalLight &light) const;

	/// <summary>
	/// 物体全体の変形情報を設定する
	/// </summary>
	/// <param name="transform">拡大率・回転角・移動量</param>
	void SetTransform(const Transform &transform);

	/// <summary>
	/// 物体全体の変形情報を取得する
	/// </summary>
	/// <returns>変形情報への読み取り専用参照</returns>
	const Transform &GetTransform() const;

	/// <summary>
	/// 材質数を取得する
	/// </summary>
	/// <returns>材質数。未初期化の場合は0</returns>
	std::size_t GetMaterialCount() const;

	/// <summary>
	/// 編集用の材質情報を取得する。変更は次のUpdateでGPUへ反映する
	/// </summary>
	/// <param name="materialIndex">0以上、GetMaterialCount()未満の材質番号</param>
	/// <returns>CPU側の材質情報への参照</returns>
	MaterialData &GetMaterialData(std::size_t materialIndex);

	/// <summary>
	/// 読み取り専用の材質情報を取得する
	/// </summary>
	/// <param name="materialIndex">0以上、GetMaterialCount()未満の材質番号</param>
	/// <returns>CPU側の材質情報への読み取り専用参照</returns>
	const MaterialData &GetMaterialData(std::size_t materialIndex) const;

	/// <summary>
	/// 物体ごとのリソースを解放する。GPUの利用完了後、共有モデルより先に呼び出す
	/// </summary>
	void Finalize();

  private:
	const Model *model_ = nullptr;

	Transform transform_{};

	std::vector<Material> materials_;
	std::vector<MaterialData> materialData_;

	std::vector<MeshInstance> meshInstances_;
};
