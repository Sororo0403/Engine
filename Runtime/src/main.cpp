#include "core/Engine.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/StringConversion.h"
#include "scene/RuntimeScene.h"
#include "scene/SceneSerializer.h"
#include <Windows.h>
#include <exception>
#include <fstream>
#include <memory>
#include <string_view>

namespace {
GameConfig game;

/// <summary>
/// 起動ウィンドウを作らず、実行ファイルと同じ場所へビルド構成と対応形式を記録する
/// </summary>
/// <returns>0:成功 1:ファイルを記録できない</returns>
int WriteBuildInfo() {
	try {
#ifdef _DEBUG
		const char *configuration = "Debug";
#else
		const char *configuration = "Release";
#endif
#ifdef _M_X64
		const char *architecture = "x64";
#else
		const char *architecture = "unsupported";
#endif
		std::ofstream file(PathUtils::GetExecutableDirectory() /
		                       L"Runtime.build.json",
		                   std::ios::binary | std::ios::trunc);
		file << "{\"configuration\":\"" << configuration
		     << "\",\"architecture\":\"" << architecture
		     << "\",\"gameConfigVersion\":" << GameConfig::kCurrentVersion
		     << ",\"sceneVersion\":" << SceneSerializer::kCurrentVersion
		     << "}\n";
		file.close();
		return file ? 0 : 1;
	} catch (const std::exception &) {
		return 1;
	}
}

/// <summary>
/// 出力したゲームの最初のシーンを作成する
/// </summary>
/// <param name="context">共通機能</param>
/// <returns>初期化前の実行シーン</returns>
std::unique_ptr<IScene> CreateScene(const EngineContext &context) {
	return std::make_unique<RuntimeScene>(context, game);
}
} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR commandLine, int) {
	if (commandLine && std::string_view(commandLine) == "--write-build-info") {
		return WriteBuildInfo();
	}

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
