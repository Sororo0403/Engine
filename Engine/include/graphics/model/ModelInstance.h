#pragma once
#include "animation/ModelAnimationSampler.h"
#include <array>
#include <optional>
#include <span>

#include "graphics/material/ColorTint.h"

#include "graphics/material/Material.h"
#include "graphics/mesh/MeshInstance.h"

#include <cstddef>
#include <vector>

class Camera;
class DirectionalLight;
class PointLights;
class SpotLights;
class ShadowMap;
class MeshRenderer;
struct MeshFogData;
class Model;
struct Ray;

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
	/// <param name="world">親子階層を合成したワールド行列。せん断を含めて反映する</param>
	/// <param name="tint">元の材質へ掛ける色。材質データ自体には累積しない</param>
	/// <returns>true:成功 false:未初期化、または材質更新失敗</returns>
	bool Update(const Camera &camera, const DirectX::XMFLOAT4X4 &world,
	            const ColorTint &tint = {});

	/// <summary>
	/// ワールド専用の画像・影のヒープを使用し、各メッシュの描画命令を記録する
	/// </summary>
	/// <param name="commandList">記録中のコマンドリスト</param>
	/// <param name="renderer">初期化済みのメッシュ描画クラス</param>
	/// <param name="pointLights">更新済みの点光源配列</param>
	/// <param name="shadows">このワールド専用の影と材質画像の参照</param>
	/// <param name="fog">カメラ位置と距離霧の定数</param>
	/// <param name="spotLights">更新済みのスポットライト配列</param>
	/// <param name="light">更新済みの平行光源</param>
	void Draw(ID3D12GraphicsCommandList *commandList, MeshRenderer &renderer,
	          const DirectionalLight &light, const PointLights &pointLights,
	          const SpotLights &spotLights, const ShadowMap &shadows,
	          const MeshFogData &fog) const;

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

	/// <summary>
	/// 指定スロットの共有材質を差し替える。メッシュと変形は変更しない
	/// </summary>
	/// <param name="slot">材質スロット番号</param>
	/// <param name="resource">独立した共有材質。空はモデルの既定材質へ戻す</param>
	/// <returns>true:成功 false:物体やスロット、材質が無効</returns>
	bool SetMaterial(std::size_t slot,
	                 const std::shared_ptr<const MaterialResource> &resource);

	/// <summary>
	/// 光源からの深度描画と材質の準備に使用する、読み取り専用のメッシュ一覧を取得する
	/// </summary>
	/// <returns>次の初期化・終了まで有効な一覧</returns>
	std::span<const MeshInstance> GetMeshes() const;

	/// <summary>
	/// 同じモデルの再生姿勢を物体専用の骨バッファへ設定する
	/// </summary>
	/// <param name="pose">ノード・骨の評価結果。nullptrは従来の静止描画へ戻す</param>
	/// <returns>true:成功 false:再生データなし、姿勢・GPU作成が無効</returns>
	/// <remarks>GPUの利用完了後、描画準備より前に呼ぶ。共有モデルと他の物体は変更しない</remarks>
	bool SetAnimationPose(const ModelAnimationPose *pose);

	/// <summary>
	/// 現在の静止またはスキン姿勢を囲むモデル座標の範囲を取得する
	/// </summary>
	/// <param name="corners">成功時だけ設定する8頂点</param>
	/// <returns>true:成功 false:未初期化または無効な姿勢</returns>
	bool GetBoundsCorners(std::array<DirectX::XMFLOAT3, 8> &corners) const;

	/// <summary>
	/// 現在の描画姿勢の三角形をレイで選択する
	/// </summary>
	/// <param name="ray">モデル座標のレイ</param>
	/// <param name="distance">交差した時だけ置き換える距離</param>
	/// <returns>true:交差 false:未初期化・無効・交差なし</returns>
	bool Raycast(const Ray &ray, float &distance) const;

  private:
	const Model *model_ = nullptr;

	std::vector<Material> materials_;
	std::vector<MaterialData> materialData_;

	std::vector<MeshInstance> meshInstances_;
	std::vector<MeshInstance> animatedInstances_;
	std::optional<ModelAnimationPose> animationPose_;
	std::array<DirectX::XMFLOAT3, 8> animationBounds_{};
	ID3D12Device *device_ = nullptr;
};
