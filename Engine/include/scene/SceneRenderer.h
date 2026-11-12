#pragma once
#include "animation/AnimatorDrawPose.h"
#include "particles/ParticleDrawBatch.h"
#include "ui/UIButtonVisual.h"
#include <vector>

#include "graphics/lighting/LightingSettings.h"
#include "graphics/model/ModelHandle.h"
#include <cstdint>

#include "graphics/camera/CameraPose.h"
#include "graphics/camera/CameraSettings.h"
#include "graphics/render/DebugLine.h"
#include "graphics/render/RenderObjectHandle.h"
#include "scene/SceneObjectId.h"

#include <DirectXMath.h>
#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <string>

class AudioClipManager;
class ModelManager;
class RenderSystem;
class RenderWorld;
class Scene;
struct SceneBackgroundFreeze;
class RenderTexture;
class CanvasRenderer;
class ParticleRenderer;
struct Ray;

/// <summary>
/// シーンの物体データを描画ワールドへ反映し、描画を実行する
/// </summary>
class SceneRenderer {
  public:
	/// <summary>
	/// 検証した照明を描画ワールドへ反映する
	/// </summary>
	/// <param name="fallback">シーン固有の設定がない場合に使う照明設定</param>
	/// <returns>true:成功 false:未初期化または設定が無効。以前の照明は保持する</returns>
	bool SetLighting(const LightingSettings &fallback);

	/// <summary>
	/// 読み込み不能なモデルに対する描画更新の扱い
	/// </summary>
	enum class ModelLoadFailure { FAIL, SKIP };

	/// <summary>
	/// 描画対象のシーンと共通機能への参照を保持する。
	/// GPUリソースは作成しない
	/// </summary>
	/// <param name="scene">描画対象。このクラスより長く保持すること</param>
	/// <param name="models">モデル管理。このクラスより長く保持すること</param>
	/// <param name="render">描画機能。このクラスより長く保持すること</param>
	/// <param name="failure">FAIL:読み込み失敗で更新失敗 SKIP:参照を保持して対象の描画だけ省略</param>
	SceneRenderer(const Scene &scene, ModelManager &models,
	              RenderSystem &render,
	              ModelLoadFailure failure = ModelLoadFailure::FAIL);

	~SceneRenderer();

	SceneRenderer(const SceneRenderer &) = delete;
	SceneRenderer &operator=(const SceneRenderer &) = delete;

	/// <summary>
	/// シーン専用の描画ワールドを初期化する
	/// </summary>
	/// <returns>true:成功 false:初期化済み、または作成失敗</returns>
	bool Initialize();

	/// <summary>
	/// 物体データを反映し、GPU用データを更新する。
	/// 全ての編集後、GPUの利用完了後、BeginFrame前に呼び出す
	/// </summary>
	/// <param name="width">描画領域の幅。高さと共に0の場合はウィンドウサイズ</param>
	/// <param name="height">描画領域の高さ。幅と共に0の場合はウィンドウサイズ</param>
	/// <returns>true:成功 false:未初期化、または更新失敗</returns>
	bool Update(int width = 0, int height = 0);

	/// <summary>
	/// シーンを描画する。
	/// Update成功後、BeginFrameとEndFrameの間に一度呼び出す
	/// </summary>
	/// <param name="target">描画先。nullptrの場合は現在の描画先を使用する</param>
	void Draw(RenderTexture *target = nullptr) const;

	/// <summary>
	/// 描画リソースを解放する。GPUの利用完了後に呼び出す
	/// </summary>
	void Finalize();

	/// <summary>
	/// 最後の更新で、モデルを読み込めず描画を省略した物体か確認する
	/// </summary>
	/// <param name="id">シーンの物体ID</param>
	/// <returns>true:SKIPで省略中 false:描画可能・非表示・対象なし・まだ更新されていない</returns>
	/// <remarks>キャッシュが追加・再読み込みされた後の更新で再試行する。同じ失敗を毎フレーム繰り返さない</remarks>
	bool IsModelUnavailable(SceneObjectId id) const;

	/// <summary>
	/// シーンの描画に使用するカメラの姿勢を設定する。
	/// Updateより前に呼び出す
	/// </summary>
	/// <param name="pose">設定する姿勢</param>
	/// <returns>true:成功 false:未初期化、または姿勢が無効</returns>
	bool SetCameraPose(const CameraPose &pose);

	/// <summary>
	/// 描画カメラの投影設定を次の更新へ反映する
	/// </summary>
	/// <param name="settings">有効な視野角と表示距離</param>
	/// <returns>true:成功 false:未初期化、または設定が無効</returns>
	bool SetCameraSettings(const CameraSettings &settings);

	/// <summary>
	/// 画像内の位置から、最も近くで交差するシーン上の物体を取得する
	/// </summary>
	/// <param name="imageX">画像内の横位置。左端0、右端1</param>
	/// <param name="imageY">画像内の縦位置。上端0、下端1</param>
	/// <param name="distance">指定時はレイ始点から交点までのワールド距離を設定する。交差なしの場合は保持する</param>
	/// <param name="excluded">交差判定から除く物体ID。不在・重複したIDは結果へ影響しない</param>
	/// <returns>除外されていない物体のID。交差なしの場合は0</returns>
	/// <remarks>Update成功後、Drawより前に呼び出す</remarks>
	SceneObjectId Pick(float imageX, float imageY, float *distance = nullptr,
	                   std::span<const SceneObjectId> excluded = {}) const;

	/// <summary>
	/// シーンの描画カメラから、画像内の位置を通るレイを作成する
	/// </summary>
	/// <param name="imageX">画像内の横位置。左端0、右端1</param>
	/// <param name="imageY">画像内の縦位置。上端0、下端1</param>
	/// <param name="ray">成功した場合に設定するワールド座標のレイ</param>
	/// <returns>true:成功 false:更新前、または位置が無効</returns>
	bool CreatePickingRay(float imageX, float imageY, Ray &ray) const;

	/// <summary>
	/// シーンの描画に使用する更新済みのカメラ行列を取得する
	/// </summary>
	/// <param name="view">成功した場合に設定するビュー行列</param>
	/// <param name="projection">成功した場合に設定する投影行列</param>
	/// <returns>true:成功 false:未初期化、または更新前</returns>
	/// <remarks>Update成功後、Drawより前に呼び出す</remarks>
	bool GetCameraMatrices(DirectX::XMFLOAT4X4 &view,
	                       DirectX::XMFLOAT4X4 &projection) const;

	/// <summary>
	/// シーン上の物体を囲むボックスの頂点をワールド座標で取得する
	/// </summary>
	/// <param name="objectId">対象物体のID</param>
	/// <param name="corners">成功した場合に設定する8頂点</param>
	/// <returns>true:成功 false:未初期化、物体なし、非表示、Meshなし、またはMesh Rendererなし</returns>
	/// <remarks>シーンの変更をUpdateで反映した後に呼び出す</remarks>
	bool GetObjectBounds(SceneObjectId objectId,
	                     std::array<DirectX::XMFLOAT3, 8> &corners) const;

	/// <summary>
	/// 今回のシーン描画で表示する線を登録する
	/// </summary>
	/// <param name="line">ワールド座標の線</param>
	/// <returns>true:成功 false:未初期化、更新前、または線が無効</returns>
	/// <remarks>最後のUpdate成功後に登録し、全て登録後にUpdateDebugLinesを呼び出す</remarks>
	bool AddDebugLine(const DebugLine &line);

	/// <summary>
	/// 登録された線をGPU用の頂点バッファへ転送する。
	/// GPUの利用完了後、BeginFrame前に呼び出す
	/// </summary>
	/// <returns>true:成功 false:未初期化、更新前、または転送失敗</returns>
	/// <remarks>最後のAddDebugLine後、Drawより前に呼び出す</remarks>
	bool UpdateDebugLines();

	/// <summary>
	/// 3Dの後に画面上のCanvasを描くか指定する。Scene編集ビューでは無効にする
	/// </summary>
	/// <param name="enabled">true:UIを表示 false:3Dだけ表示</param>
	/// <remarks>次のUpdateより前に呼ぶ</remarks>
	void SetScreenUIEnabled(bool enabled) {
		screenUIEnabled_ = enabled;
	}

	/// <summary>
	/// 実行中のボタンの表示を次の更新へ渡す。編集プレビューでは空の一覧を使う
	/// </summary>
	/// <param name="visuals">表示対象シーンの重複しないButton状態</param>
	/// <returns>true:反映成功 false:物体・状態が無効。以前の一覧を保持する</returns>
	bool SetUIButtonVisuals(std::span<const UIButtonVisual> visuals);

	/// <summary>
	/// Canvasで使用中の指定画像を読み直す
	/// </summary>
	bool ReloadCanvasImage(const std::filesystem::path &path, std::string &error);

	/// <summary>
	/// 先読みを指定したモデルを、物体の表示状態に関係なく準備する
	/// </summary>
	/// <param name="error">成功時は空、失敗時は対象と理由</param>
	/// <returns>true:準備成功 false:モデルの読み込み失敗</returns>
	/// <remarks>シーン読み込み後、最初の描画前に呼ぶ</remarks>
	bool PrepareSceneModels(std::string &error);

	/// <summary>
	/// 宣言されたPrefabのモデル・材質・音声・UI画像を生成前に準備する
	/// </summary>
	/// <param name="prefabs">再生中に共有する不変のPrefab階層</param>
	/// <param name="clips">共通の音声素材管理</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:準備成功 false:素材が無効、またはGPU準備失敗</returns>
	/// <remarks>GPU完了後、BeginFrame前に呼ぶ。生成用画像は停止時にClearPrefabResourcesで解放する</remarks>
	bool PreparePrefabResources(
	    std::span<const std::shared_ptr<const Scene>> prefabs,
	    AudioClipManager &clips, std::string &error);

	/// <summary>
	/// 生成用の階層とUI画像の保持を解除する。GPU完了後に呼ぶ
	/// </summary>
	void ClearPrefabResources();

	/// <summary>
	/// 次の更新で描く粒子のスナップショットを受け取る
	/// </summary>
	/// <param name="batches">移動・物理・Luaを反映したワールド座標の粒子</param>
	void SetParticles(std::vector<ParticleDrawBatch> batches);

	/// <summary>
	/// 同じ再生シーンの骨・ノード姿勢を受け取る。次のUpdateで描画・影・選択へ反映する
	/// </summary>
	/// <param name="poses">モデルと姿勢の変更しない共有参照。空は全物体を静止描画へ戻す</param>
	/// <returns>true:成功 false:物体・モデル・重複が無効。失敗時は以前の一覧を保持する</returns>
	bool SetAnimatorPoses(std::vector<AnimatorDrawPose> poses);

  private:
	/// <summary>
	/// シーン上の物体と対応する描画物体の情報
	/// </summary>
	struct Object {
		std::filesystem::path modelPath;
		RenderObjectHandle handle;
		ModelHandle model;
		bool unavailable = false;
		std::uint64_t failedCacheRevision = 0;
	};

	/// <summary>
	/// 描画物体の追加・削除・モデル変更・変形情報を反映する
	/// </summary>
	/// <returns>true:成功 false:モデル読み込み、または物体作成失敗</returns>
	bool SynchronizeObjects(bool keepBackground);

	// 参照先はSceneRendererより長く保持する
	const Scene &scene_;
	LightingSettings fallbackLighting_;
	ModelManager &models_;
	RenderSystem &render_;
	const ModelLoadFailure modelLoadFailure_;

	std::unique_ptr<RenderWorld> world_;
	std::unique_ptr<CanvasRenderer> canvas_;
	std::unique_ptr<ParticleRenderer> particles_;
	std::vector<ParticleDrawBatch> particleBatches_;
	std::vector<ParticleDrawBatch> frozenParticles_;
	std::shared_ptr<const SceneBackgroundFreeze> backgroundFreeze_;
	std::vector<SceneObjectId> frozenForeground_;
	std::map<SceneObjectId, AnimatorDrawPose> animatorPoses_;
	std::vector<std::shared_ptr<const Scene>> preparedPrefabs_;
	bool screenUIEnabled_ = true;
	bool drawCanvas_ = false;
	std::vector<UIButtonVisual> buttonVisuals_;

	// シーン上の物体IDと、対応する描画物体の情報
	std::map<SceneObjectId, Object> objects_;
	std::uint64_t modelRevision_ = 0;
};
