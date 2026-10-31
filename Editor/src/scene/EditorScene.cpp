#include "scene/EditorScene.h"

#include "core/EngineContext.h"
#include "core/Logger.h"

#include <cmath>

EditorScene::EditorScene(const EngineContext &context)
    : renderer_(scene_, context.models, context.render) {}

EditorScene::~EditorScene() = default;

bool EditorScene::Initialize() {
	if (!renderer_.Initialize()) {
		return false;
	}

	Logger::Info("EditorSceneを初期化しました。");
	return true;
}

bool EditorScene::Update(float deltaTime) {
	if (!std::isfinite(deltaTime) || deltaTime < 0.0f) {
		Logger::Error("EditorScene::Update: 経過時間が無効です。");
		return false;
	}

	// HierarchyやInspectorによる編集処理は、この位置に追加する

	// 全ての編集を終えてから描画側へ反映する
	return renderer_.Update();
}

void EditorScene::Draw() const { renderer_.Draw(); }

void EditorScene::Finalize() {
	// GPUの利用完了後、参照している描画側から終了する
	renderer_.Finalize();

	scene_.Clear();
	selectedObjectId_ = 0;
}
