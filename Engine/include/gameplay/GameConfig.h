#pragma once

#include "gameplay/GameInputBindings.h"
#include "graphics/camera/CameraPose.h"
#include <filesystem>
#include <string>

/// <summary>
/// 出力したゲームの起動シーン・ウィンドウ・カメラ・操作キーの初期設定
/// </summary>
struct GameConfig {
	std::string title = "Game";
	int width = 1280, height = 720;
	std::filesystem::path scenePath = L"resources/game/scene.json";
	CameraPose fallbackCamera{};
	GameInputBindings input;

	/// <summary>
	/// 起動値と、パッケージ内を指す相対シーンパスを確認する
	/// </summary>
	/// <returns>true:有効 false:範囲外またはパス・カメラが無効</returns>
	bool IsValid() const;

	/// <summary>
	/// 起動設定を検証し、ファイルへ書き込まずversion 2のJSON文字列に変換する
	/// </summary>
	/// <param name="text">成功時に置き換えるJSON文字列。失敗時は保持する</param>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <returns>true:成功 false:設定が無効</returns>
	bool Serialize(std::string &text, std::string &error) const;

	/// <summary>
	/// 起動設定を新規の一時ファイルへ書き込み、完了後に保存先を置き換える
	/// </summary>
	/// <param name="path">書き込み先。相対パスは作業ディレクトリ基準。親フォルダは事前に作成する</param>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <returns>true:成功 false:設定またはファイル操作が無効</returns>
	bool Save(const std::filesystem::path &path, std::string &error) const;

	/// <summary>
	/// version 1・2を読み込み、全設定の検証成功後だけ現在値を置き換える。version 1の操作キーはWASDとSpace
	/// </summary>
	/// <param name="path">読み込む設定ファイル</param>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <returns>true:成功 false:無効なデータ。失敗時は現在値を保持する</returns>
	bool Load(const std::filesystem::path &path, std::string &error);
};
