#pragma once

#include "gameplay/GameConfig.h"

#include <filesystem>
#include <string>

/// <summary>
/// ゲームプロジェクトのルート・起動シーン・ゲーム設定を管理する
/// </summary>
class ProjectDocument {
  public:
	/// <summary>
	/// 選択したフォルダにプロジェクトを作成する。既存のファイルは上書きしない
	/// </summary>
	bool Create(const std::filesystem::path &folder, std::string &error);

	/// <summary>
	/// project.jsonを読み込み、設定とフォルダの検証に成功した場合だけ反映する
	/// </summary>
	bool Open(const std::filesystem::path &folder, std::string &error);

	/// <summary>
	/// ゲーム設定と起動シーンを保存する。空のシーンパスは起動シーンの未指定を表す
	/// </summary>
	bool Save(const GameConfig &game, const std::filesystem::path &startupScene,
	          std::string &error);

	const std::filesystem::path &GetRoot() const;
	std::filesystem::path GetAssetsDirectory() const;
	std::filesystem::path GetScenesDirectory() const;
	std::filesystem::path GetStartupScene() const;
	const GameConfig &GetGameConfig() const;

  private:
	std::filesystem::path root_;
	std::filesystem::path startupScene_;
	GameConfig game_;
};
