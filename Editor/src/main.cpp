#include "core/Engine.h"
#include "scene/EditorScene.h"

#include <Windows.h>
#include <memory>

namespace {

/// <summary>
/// 最初に表示する編集シーンを生成する
/// </summary>
/// <param name="context">初期化済みの共通機能</param>
/// <returns>未初期化の編集シーン</returns>
std::unique_ptr<IScene> CreateInitialScene(const EngineContext &context) {
	return std::make_unique<EditorScene>(context);
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	EngineConfig config;
	config.windowTitle = L"Editor";
	config.windowWidth = 1600;
	config.windowHeight = 900;

	return Engine::Run(config, CreateInitialScene);
}
