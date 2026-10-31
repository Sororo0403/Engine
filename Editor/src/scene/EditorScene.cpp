#include "scene/EditorScene.h"

#include "core/EngineContext.h"
#include "core/Logger.h"
#include "graphics/ui/ImGuiSystem.h"
#include "graphics/render/RenderTexture.h"

#include <cmath>

EditorScene::EditorScene(const EngineContext &context)
    : ui_(context.ui), input_(context.input),
      renderer_(scene_, context.models, context.render),
      sceneViewPanel_(context.render) {}

EditorScene::~EditorScene() = default;

bool EditorScene::Initialize() {
	if (!ui_.EnableDocking()) {
		return false;
	}

	if (!renderer_.Initialize()) {
		return false;
	}

	cameraController_.Reset();

	// カメラ操作とInspectorの確認用にモデルを配置する
	selectedObjectId_ =
	    scene_.CreateObject("Box", L"resources/editor/models/box.glb");

	if (auto *object = scene_.FindObject(selectedObjectId_)) {
		object->transform.scale = {0.45f, 0.45f, 0.45f};
		object->transform.rotation = {0.3f, 0.4f, 0.0f};
	}

	Logger::Info("EditorSceneを初期化しました。");
	return true;
}

bool EditorScene::Update(float deltaTime) {
	if (!std::isfinite(deltaTime) || deltaTime < 0.0f) {
		Logger::Error("EditorScene::Update: 経過時間が無効です。");
		return false;
	}

	// 配置対象のパネルを表示する前に、ドッキング領域を作成する
	ui_.DrawDockSpace(dockLayout_, resetDockLayout_);
	resetDockLayout_ = false;

	hierarchyPanel_.Draw(ui_, scene_, selectedObjectId_);
	inspectorPanel_.Draw(ui_, scene_, selectedObjectId_);

	if (!sceneViewPanel_.DrawUI(ui_)) {
		return false;
	}

	// 非表示の場合も更新し、継続中のカメラ操作を終了させる
	cameraController_.Update(input_, sceneViewPanel_.GetInput(), deltaTime);

	const auto *target = sceneViewPanel_.GetRenderTexture();

	if (!target) {
		return true;
	}

	// カメラの姿勢を設定してから描画用データを更新する
	if (!renderer_.SetCameraPose(cameraController_.GetPose())) {
		Logger::Error("EditorScene: カメラの姿勢が無効です。");
		return false;
	}

	return renderer_.Update(target->GetWidth(), target->GetHeight());
}

void EditorScene::Draw() const {
	auto *target = sceneViewPanel_.GetRenderTexture();

	if (target) {
		renderer_.Draw(target);
	}
}

void EditorScene::Finalize() {
	// GPUの利用完了後に描画リソースを解放する
	sceneViewPanel_.Finalize();
	renderer_.Finalize();

	scene_.Clear();
	selectedObjectId_ = 0;

	cameraController_.Reset();
}
