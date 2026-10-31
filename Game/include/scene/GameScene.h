#pragma once

#include "scene/IScene.h"

#include "graphics/model/ModelHandle.h"
#include "graphics/render/RenderObjectHandle.h"

#include <array>
#include <cstddef>
#include <memory>

struct EngineContext;

class DebugUI;
class InputSystem;
class ModelManager;
class RenderSystem;
class RenderWorld;

/// <summary>
/// ゲーム画面の物体配置・カメラ操作・調整UIを管理する
/// </summary>
class GameScene final : public IScene {
  private:
	static constexpr std::size_t kObjectCount = 2;

  public:
	/// <summary>
	/// ゲーム画面が使用する共通機能への参照を受け取る
	/// </summary>
	/// <param name="context">初期化済みの共通機能</param>
	explicit GameScene(const EngineContext &context);

	~GameScene() override;

	GameScene(const GameScene &) = delete;
	GameScene &operator=(const GameScene &) = delete;

	/// <summary>
	/// 描画ワールド・モデル・物体を初期化する
	/// </summary>
	/// <returns>true:成功 false:初期化済み、または作成失敗</returns>
	bool Initialize() override;

	/// <summary>
	/// 調整UIを構築し、物体・カメラ・描画用データを更新する。UIフレーム開始後に呼び出す
	/// </summary>
	/// <param name="deltaTime">前フレームからの経過時間。単位は秒</param>
	/// <returns>true:成功 false:未初期化、引数が無効、または更新失敗</returns>
	bool Update(float deltaTime) override;

	/// <summary>
	/// 全物体を描画する。描画基盤のBeginFrame後に呼び出す
	/// </summary>
	void Draw() const override;

	/// <summary>
	/// シーンのリソースを解放する。GPUの利用完了後に呼び出す
	/// </summary>
	void Finalize() override;

  private:
	/// <summary>
	/// モデルマネージャーから共有モデルを取得する
	/// </summary>
	/// <returns>true:成功 false:読み込み失敗</returns>
	bool CreateModel();

	/// <summary>
	/// 描画物体を作成して初期配置を設定する
	/// </summary>
	/// <returns>true:成功 false:作成失敗</returns>
	bool CreateInstances();

	/// <summary>
	/// キーボード操作によるカメラの移動を反映する
	/// </summary>
	/// <param name="deltaTime">前フレームからの経過時間。単位は秒</param>
	void UpdateCamera(float deltaTime);

	/// <summary>
	/// ライトと物体の調整用UIを構築する
	/// </summary>
	void DrawDebugControls();

	// 参照先はGameSceneより長く保持する
	ModelManager &models_;
	RenderSystem &render_;
	const InputSystem &input_;
	const DebugUI &debugUI_;

	// ワールドより先に宣言し、ワールドの破棄後まで保持する
	ModelHandle model_;
	std::unique_ptr<RenderWorld> world_;

	std::array<RenderObjectHandle, kObjectCount> objects_{};

	bool autoRotate_ = true;
	bool initialized_ = false;
};
