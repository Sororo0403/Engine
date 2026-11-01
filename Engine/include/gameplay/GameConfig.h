#pragma once
#include "graphics/camera/CameraPose.h"
#include <filesystem>
#include <string>

/// <summary>
/// 出力したゲームの起動シーン・ウィンドウ・カメラの初期設定
/// </summary>
struct GameConfig {
	std::string title = "Game";
	int width = 1280, height = 720;
	std::filesystem::path scenePath = L"resources/game/scene.json";
	CameraPose fallbackCamera{};
	/// <summary>
	/// 起動値と、パッケージ内を指す相対シーンパスを確認する
	/// </summary>
	/// <returns>true:有効 false:範囲外またはパス・カメラが無効</returns>
	bool IsValid() const;
	/// <summary>
	/// 起動設定をファイルへ保存する
	/// </summary>
	/// <param name="path">書き込み先</param>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <returns>true:成功 false:設定またはファイル操作が無効</returns>
	bool Save(const std::filesystem::path &path, std::string &error) const;
	/// <summary>
	/// 全設定の検証に成功した場合だけ現在値を置き換える
	/// </summary>
	/// <param name="path">読み込む設定ファイル</param>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <returns>true:成功 false:無効なデータ。失敗時は現在値を保持する</returns>
	bool Load(const std::filesystem::path &path, std::string &error);
};
