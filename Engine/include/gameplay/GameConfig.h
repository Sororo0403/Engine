#pragma once

#include "gameplay/GameActionBinding.h"
#include "gameplay/GameInputBindings.h"
#include "graphics/lighting/LightingSettings.h"
#include "graphics/camera/CameraPose.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

/// <summary>
/// 出力したゲームの起動シーン・ウィンドウ・カメラ・操作キー・ゲームパッド・照明の初期設定
/// </summary>
struct GameConfig {
	/// <summary>
	/// 保存する形式のバージョン。読み込みはversion 1からこの値まで対応する
	/// </summary>
	static constexpr int kCurrentVersion = 12;

	std::string title = "Game";
	int width = 1280, height = 720;
	std::filesystem::path scenePath = L"resources/game/scene.json";
	CameraPose fallbackCamera{};
	GameInputBindings input;
	std::vector<GameActionBinding> actions;
	LightingSettings lighting;
	bool cameraRelativeMovement = false;
	bool gamepadEnabled = true;

	/// <summary>
	/// ユーザー設定の保存先を識別するIDと、保存するスクリプト共有テーブルのキー
	/// </summary>
	std::string settingsId;
	std::vector<std::string> persistentScriptKeys;

	/// <summary>
	/// 起動値と、パッケージ内を指す相対シーンパスを確認する
	/// </summary>
	/// <returns>true:有効 false:範囲外またはパス・カメラ・照明が無効</returns>
	bool IsValid() const;

	/// <summary>
	/// ウィンドウのタイトルとサイズを検証する
	/// </summary>
	bool HasValidWindowSettings() const;

	/// <summary>
	/// ウィンドウのタイトルに使用できるUTF-8文字列か確認する
	/// </summary>
	bool HasValidWindowTitle() const;

	/// <summary>
	/// ウィンドウの幅または高さが有効な範囲内か確認する
	/// </summary>
	static bool IsValidWindowDimension(int value);

	/// <summary>
	/// シーン、カメラ、入力、照明、永続化の起動設定を検証する
	/// </summary>
	bool HasValidStartupSettings() const;

	/// <summary>
	/// 起動設定を検証し、ファイルへ書き込まず現在の形式のJSON文字列に変換する
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
	/// <param name="overwrite">既存の保存先を置き換えるか</param>
	/// <returns>true:成功 false:設定またはファイル操作が無効</returns>
	bool Save(const std::filesystem::path &path, std::string &error,
	          bool overwrite = true) const;

	/// <summary>
	/// 旧形式にも対応し、全設定の検証成功後だけ現在値を置き換える。version 1の操作キーはWASDとSpace、version 1・2の移動はワールド基準、version 1から3の照明は従来の初期値、version 1から4のゲームパッドは有効。version 1から5の操作キーはEを優先し、既存の割り当てと重複しないキーを選ぶ。version 1から6の名前付き操作は空。version 1から7の影は無効。version 1から8のゲームパッド軸は未割り当て。version 10以前ではスクリプトの設定を永続化しない
	/// </summary>
	/// <param name="path">読み込む設定ファイル</param>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <returns>true:成功 false:無効なデータ。失敗時は現在値を保持する</returns>
	bool Load(const std::filesystem::path &path, std::string &error);

	/// <summary>
	/// JSON文字列を検証し、全設定の読み込み成功後だけ現在値を置き換える
	/// </summary>
	/// <param name="text">ファイル操作を行わず読み込む起動設定</param>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <returns>true:成功 false:無効なデータ。失敗時は以前の設定を保持する</returns>
	bool Deserialize(std::string_view text, std::string &error);
};
