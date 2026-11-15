#include "core/Engine.h"
#include "core/Logger.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/StringConversion.h"
#include "scene/RuntimeScene.h"
#include "scene/SceneSerializer.h"
#include "scripting/LuaScript.h"
#include <Windows.h>
#include <exception>
#include <fstream>
#include <memory>
#include <string_view>

namespace {
GameConfig game;
RuntimeScene::SaveMode saveMode = RuntimeScene::SaveMode::NORMAL;

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
		     << ",\"scriptApiVersion\":" << LuaScript::kApiVersion
		     << ",\"supportsFreshStart\":true}\n";
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
	return std::make_unique<RuntimeScene>(context, game, saveMode);
}
} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR commandLine, int) {
	const std::string_view arguments = commandLine ? commandLine : "";
	if (arguments == "--write-build-info") {
		return WriteBuildInfo();
	}
	const bool freshStart = arguments == "--editor-launch-fresh";
	const bool launchedFromEditor = freshStart || arguments == "--editor-launch";
	saveMode = freshStart ? RuntimeScene::SaveMode::SESSION_ONLY
	                      : launchedFromEditor ? RuntimeScene::SaveMode::EDITOR_TEST
	                                           : RuntimeScene::SaveMode::NORMAL;

	std::string error;
	const auto executableDirectory = PathUtils::GetExecutableDirectory();

	if (!game.Load(executableDirectory / L"game.json", error)) {
		// Engine::Runの前に失敗した場合も、配布結果画面から原因を読めるようにする。
		if (!executableDirectory.empty() &&
		    Logger::Initialize(PathUtils::ToUtf8(executableDirectory / L"logs" /
		                                         L"Engine.log"))) {
			Logger::Error("Could not load game.json: " + error);
			Logger::Finalize();
		}
		if (!launchedFromEditor) {
			const auto text = StringConversion::Utf8ToWide(error);
			MessageBoxW(nullptr, text.c_str(), L"Could not start game",
			            MB_OK | MB_ICONERROR);
		}
		return -1;
	}

	EngineConfig config;
	config.windowTitle = StringConversion::Utf8ToWide(game.title);
	config.windowWidth = game.width;
	config.windowHeight = game.height;
	config.enableUI = game.enableScriptEditing;
	config.preferLowPowerGpu = arguments == "--low-power-gpu";
	const auto result = Engine::Run(config, CreateScene);

	if (result != 0 && !launchedFromEditor) {
		MessageBoxW(nullptr, L"Could not run the game. See logs/Engine.log.",
		            L"Game", MB_OK | MB_ICONERROR);
	}

	return result;
}
