#include "core/Engine.h"
#include "scene/GameScene.h"

#include <Windows.h>
#include <memory>

namespace {

/// <summary>
/// 最初に表示するゲームシーンを生成する
/// </summary>
/// <param name="context">初期化済みの共通機能</param>
/// <returns>未初期化のゲームシーン</returns>
std::unique_ptr<IScene> CreateInitialScene(const EngineContext &context) {
	return std::make_unique<GameScene>(context);
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	EngineConfig config;
	config.windowTitle = L"MyGame";
	config.windowWidth = 1280;
	config.windowHeight = 720;

	return Engine::Run(config, CreateInitialScene);
}
