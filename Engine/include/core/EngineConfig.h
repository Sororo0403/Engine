#pragma once

#include <string>

/// <summary>
/// エンジン起動時の設定
/// </summary>
struct EngineConfig {
	std::wstring windowTitle = L"Application";
	int windowWidth = 1280;
	int windowHeight = 720;

	// Editorなど、ImGuiを使用するアプリだけUIを初期化する
	bool enableUI = true;
};
