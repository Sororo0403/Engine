#include "RuntimeScene.h"
#include "core/Engine.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/StringConversion.h"
#include <Windows.h>
#include <memory>

namespace {
GameConfig game;
/// <summary>
/// 出力したゲームの最初のシーンを作成する
/// </summary>
/// <param name="context">共通機能</param>
/// <returns>初期化前の実行シーン</returns>
std::unique_ptr<IScene> CreateScene(const EngineContext &context) {
	return std::make_unique<RuntimeScene>(context, game);
}
} // namespace
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	std::string error;
	if (!game.Load(PathUtils::GetExecutableDirectory() / L"game.json", error)) {
		const auto text = StringConversion::Utf8ToWide(error);
		MessageBoxW(nullptr, text.c_str(), L"Could not start game",
		            MB_OK | MB_ICONERROR);
		return -1;
	}
	EngineConfig config;
	config.windowTitle = StringConversion::Utf8ToWide(game.title);
	config.windowWidth = game.width;
	config.windowHeight = game.height;
	config.enableUI = false;
	const auto result = Engine::Run(config, CreateScene);
	if (result != 0) {
		MessageBoxW(nullptr, L"Could not run the game. See logs/Engine.log.",
		            L"Game", MB_OK | MB_ICONERROR);
	}
	return result;
}
