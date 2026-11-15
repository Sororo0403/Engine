#include "TutorialScene.h"
#include "core/Engine.h"
#include <Windows.h>

namespace {

/// <summary>
/// エンジンが使う最初のシーンを作成する
/// </summary>
std::unique_ptr<IScene> CreateTutorial(const EngineContext &) {
	return std::make_unique<TutorialScene>();
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	// 第1章の課題。タイトルと起動時の大きさを変えて確認する。
	EngineConfig config;
	config.windowTitle = L"EngineTutorial";
	config.windowWidth = 1240;
	config.windowHeight = 820;
	return Engine::Run(config, CreateTutorial);
}
