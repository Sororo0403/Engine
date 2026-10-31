#pragma once

#include "camera/EditorCameraController.h"
#include "graphics/ui/DockLayout.h"
#include "panels/HierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/SceneViewPanel.h"
#include "scene/IScene.h"
#include "scene/Scene.h"
#include "scene/SceneRenderer.h"

#include <filesystem>

struct EngineContext;

class InputSystem;
class ImGuiSystem;

/// <summary>
/// エディターのシーンデータと編集処理を管理する
/// </summary>
class EditorScene final : public IScene {
  public:
	/// <summary>
	/// エディターが使用する共通機能への参照を受け取る
	/// </summary>
	/// <param name="context">初期化済みの共通機能</param>
	explicit EditorScene(const EngineContext &context);

	~EditorScene() override;

	EditorScene(const EditorScene &) = delete;
	EditorScene &operator=(const EditorScene &) = delete;

	/// <summary>
	/// 編集用シーンの描画機能を初期化する
	/// </summary>
	/// <returns>true:成功 false:初期化済み、または作成失敗</returns>
	bool Initialize() override;

	/// <summary>
	/// 編集UIを構築し、シーンデータを描画側へ反映する
	/// </summary>
	/// <param name="deltaTime">前フレームからの経過時間。単位は秒</param>
	/// <returns>true:成功 false:未初期化、引数が無効、または更新失敗</returns>
	bool Update(float deltaTime) override;

	/// <summary>
	/// 編集中のシーンを描画する。描画基盤のBeginFrame後に呼び出す
	/// </summary>
	void Draw() const override;

	/// <summary>
	/// 編集用リソースを解放する。GPUの利用完了後に呼び出す
	/// </summary>
	void Finalize() override;

  private:
	/// <summary>
	/// シーンの保存・読み込み用UIを表示する
	/// </summary>
	void DrawSceneFileControls();

  private:
	// 参照先はEditorSceneより長く保持する
	const ImGuiSystem &ui_;
	const InputSystem &input_;

	// 描画側が参照するため、SceneRendererより先に宣言する
	Scene scene_;
	std::filesystem::path sceneFilePath_ = L"scenes/Main.scene.json";
	SceneRenderer renderer_;

	// 各パネルのタイトルと、初期配置の割合
	DockLayout dockLayout_{
	    "EditorDockSpace", "Hierarchy", "Scene", "Inspector", 0.20f, 0.25f,
	};

	bool resetDockLayout_ = false;

	HierarchyPanel hierarchyPanel_;
	InspectorPanel inspectorPanel_;
	SceneViewPanel sceneViewPanel_;

	EditorCameraController cameraController_;

	SceneObjectId selectedObjectId_ = 0;
};
