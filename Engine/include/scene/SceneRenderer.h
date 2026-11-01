#pragma once

#include "graphics/camera/CameraPose.h"
#include "graphics/render/RenderObjectHandle.h"
#include "scene/SceneObjectId.h"

#include <filesystem>
#include <map>
#include <memory>

class ModelManager;
class RenderSystem;
class RenderWorld;
class Scene;
class RenderTexture;

/// <summary>
/// シーンの物体データを描画ワールドへ反映し、描画を実行する
/// </summary>
class SceneRenderer {
  public:
	/// <summary>
	/// 描画対象のシーンと共通機能への参照を保持する。
	/// GPUリソースは作成しない
	/// </summary>
	/// <param name="scene">描画対象。このクラスより長く保持すること</param>
	/// <param name="models">モデル管理。このクラスより長く保持すること</param>
	/// <param name="render">描画機能。このクラスより長く保持すること</param>
	SceneRenderer(const Scene &scene, ModelManager &models,
	              RenderSystem &render);

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
	/// シーンの描画に使用するカメラの姿勢を設定する。
	/// Updateより前に呼び出す
	/// </summary>
	/// <param name="pose">設定する姿勢</param>
	/// <returns>true:成功 false:未初期化、または姿勢が無効</returns>
	bool SetCameraPose(const CameraPose &pose);

	/// <summary>
	/// 画像内の位置から、最も近くで交差するシーン上の物体を取得する
	/// </summary>
	/// <param name="imageX">画像内の横位置。左端0、右端1</param>
	/// <param name="imageY">画像内の縦位置。上端0、下端1</param>
	/// <returns>交差した物体のID。交差なしの場合は0</returns>
	/// <remarks>Update成功後、Drawより前に呼び出す</remarks>
	SceneObjectId Pick(float imageX, float imageY) const;

  private:
	/// <summary>
	/// シーン上の物体と対応する描画物体の情報
	/// </summary>
	struct Object {
		std::filesystem::path modelPath;
		RenderObjectHandle handle;
	};

	/// <summary>
	/// 描画物体の追加・削除・モデル変更・変形情報を反映する
	/// </summary>
	/// <returns>true:成功 false:モデル読み込み、または物体作成失敗</returns>
	bool SynchronizeObjects();

	// 参照先はSceneRendererより長く保持する
	const Scene &scene_;
	ModelManager &models_;
	RenderSystem &render_;

	std::unique_ptr<RenderWorld> world_;

	// シーン上の物体IDと、対応する描画物体の情報
	std::map<SceneObjectId, Object> objects_;
};
