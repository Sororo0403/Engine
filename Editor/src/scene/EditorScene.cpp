#include "scene/EditorScene.h"

#include "core/EngineContext.h"
#include "core/Logger.h"
#include "graphics/render/RenderSystem.h"
#include "graphics/render/RenderWorld.h"

#include <cmath>

EditorScene::EditorScene(const EngineContext &context) : render_(context.render) {}

EditorScene::~EditorScene() = default;

bool EditorScene::Initialize() {
	if (world_) {
		Logger::Error("EditorScene::Initialize: 初期化済みです。");
		return false;
	}

	world_ = render_.CreateWorld();

	if (!world_) {
		Logger::Error("EditorScene: 描画ワールドの作成に失敗しました。");
		return false;
	}

	Logger::Info("EditorSceneを初期化しました。");
	return true;
}

bool EditorScene::Update(float deltaTime) {
	if (!world_) {
		Logger::Error("EditorScene::Update: 未初期化です。");
		return false;
	}

	if (!std::isfinite(deltaTime) || deltaTime < 0.0f) {
		Logger::Error("EditorScene::Update: 経過時間が無効です。");
		return false;
	}

	return render_.Update(*world_);
}

void EditorScene::Draw() const {
	if (!world_) {
		Logger::Error("EditorScene::Draw: 未初期化です。");
		return;
	}

	render_.Draw(*world_);
}

void EditorScene::Finalize() {
	// GPUの利用完了後に描画ワールドを解放する
	world_.reset();
}
