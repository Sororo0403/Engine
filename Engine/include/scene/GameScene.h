#pragma once

#include "scene/IScene.h"

#include "graphics/Camera.h"
#include "graphics/DirectionalLight.h"
#include "graphics/model/Model.h"
#include "graphics/model/ModelInstance.h"

#include <array>
#include <cstddef>

class D3D12Graphics;
class DebugUI;
class InputSystem;
class MeshRenderer;
class WinApp;

/// <summary>
/// ゲーム画面の物体・カメラ・ライトを管理する
/// </summary>
class GameScene final : public IScene {
  private:
	static constexpr std::size_t kObjectCount = 2;

  public:
	/// <summary>
	/// ゲーム画面が使用する機能への参照を受け取る
	/// </summary>
	/// <param name="graphics">初期化済みの描画基盤</param>
	/// <param name="renderer">初期化済みのメッシュ描画クラス</param>
	/// <param name="input">初期化済みの入力システム</param>
	/// <param name="app">表示先のアプリケーション</param>
	/// <param name="debugUI">初期化済みのデバッグUI</param>
	GameScene(D3D12Graphics &graphics, MeshRenderer &renderer,
	          const InputSystem &input, const WinApp &app,
	          const DebugUI &debugUI);

	GameScene(const GameScene &) = delete;
	GameScene &operator=(const GameScene &) = delete;

	/// <summary>
	/// モデル・物体・ライトを初期化する。GPUへの転送完了まで待機する
	/// </summary>
	/// <returns>true:成功 false:初期化済み、または作成失敗</returns>
	bool Initialize() override;

	/// <summary>
	/// 調整UIを構築し、カメラ・物体・ライトを更新する。UIフレーム開始後に呼び出す
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
	/// 共有モデルを読み込んでGPUリソースを作成する
	/// </summary>
	/// <returns>true:成功 false:読み込みまたは作成失敗</returns>
	bool CreateModel();

	/// <summary>
	/// 物体を作成して初期配置を設定する
	/// </summary>
	/// <returns>true:成功 false:作成失敗</returns>
	bool CreateInstances();

	/// <summary>
	/// キーボード操作を反映してカメラを更新する
	/// </summary>
	/// <param name="deltaTime">前フレームからの経過時間。単位は秒</param>
	/// <returns>true:成功 false:描画領域のサイズが無効</returns>
	bool UpdateCamera(float deltaTime);

	/// <summary>
	/// ライトと物体の調整用UIを構築する
	/// </summary>
	void DrawDebugControls();

	// 参照先はGameSceneより長く保持する
	D3D12Graphics &graphics_;
	MeshRenderer &renderer_;
	const InputSystem &input_;
	const WinApp &app_;
	const DebugUI &debugUI_;

	Model model_;
	std::array<ModelInstance, kObjectCount> instances_;

	Camera camera_;

	DirectionalLight light_;
	DirectionalLightData lightData_{};

	bool autoRotate_ = true;
	bool initialized_ = false;
};
