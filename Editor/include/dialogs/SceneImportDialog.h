#pragma once

#include "assets/SceneImporter.h"
#include "dialogs/FileDialog.h"
#include "gameplay/GameConfig.h"
#include <vector>

class AssetDatabase;
class ImGuiSystem;
class Scene;

enum class SceneImportResult { NONE, CANCELLED, ACCEPTED };

/// <summary>
/// 出力ゲームの起動設定と、シーンの参照素材を取り込む新規Assetsフォルダを選択する
/// </summary>
class SceneImportDialog {
  public:
	/// <summary>
	/// 現在のAssetsフォルダを作成先の初期値として、取り込み画面の表示を予約する
	/// </summary>
	/// <param name="assets">検索先と既存フォルダの一覧</param>
	/// <param name="folder">検索先基準の作成先。空は検索先自身</param>
	void Show(const AssetDatabase &assets, const std::filesystem::path &folder);

	/// <summary>
	/// 起動シーンと素材を取り込み、成功した場合だけ候補シーンを置き換える
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">取り込み先。失敗・取消時は保持し、履歴と保存先の更新は呼び出し側で行う</param>
	/// <returns>入力中・取消・取り込み完了。失敗時は画面と入力を保持する</returns>
	SceneImportResult Draw(const ImGuiSystem &ui, Scene &scene);

	/// <summary>
	/// 作成したAssetsフォルダを取得する
	/// </summary>
	/// <returns>ACCEPTED後に使用できるAssets検索先基準のフォルダ</returns>
	const std::filesystem::path &GetCreatedDirectory() const;

	/// <summary>
	/// 設定の取り込みを選び、取り込みに成功した場合だけ起動設定を取得する
	/// </summary>
	/// <returns>ACCEPTED後に参照する検証済み設定。それ以外はnullptr</returns>
	const GameConfig *GetImportedSettings() const;

  private:
	SceneImportOptions options_;
	GameConfig settings_;
	bool importSettings_ = false;
	bool accepted_ = false;
	FileDialog picker_;
	std::vector<std::filesystem::path> folders_;
	std::filesystem::path created_;
	std::string name_, error_;
	bool opening_ = false, selecting_ = false;
};
