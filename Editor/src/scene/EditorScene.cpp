#include "scene/EditorScene.h"

#include "core/EngineContext.h"
#include "core/Logger.h"
#include "graphics/debug/DebugUI.h"
#include "graphics/render/RenderTexture.h"

#include <cmath>

EditorScene::EditorScene(const EngineContext &context)
    : debugUI_(context.debugUI),
      renderer_(scene_, context.models, context.render),
      sceneViewPanel_(context.render) {}

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

	hierarchyPanel_.Draw(debugUI_, scene_, selectedObjectId_);
	inspectorPanel_.Draw(debugUI_, scene_, selectedObjectId_);

	if (!sceneViewPanel_.DrawUI(debugUI_)) {
		return false;
	}

	const auto *target = sceneViewPanel_.GetRenderTexture();

	// パネルが折りたたまれている間は描画の準備を行わない
	if (!target) {
		return true;
	}

	// ウィンドウ全体ではなく、Sceneの描画サイズを使用する
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
}
