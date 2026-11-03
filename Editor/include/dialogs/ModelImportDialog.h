#pragma once

#include "assets/ModelImporter.h"
#include "dialogs/FileDialog.h"
#include <vector>
class AssetDatabase;
class ImGuiSystem;
class ModelManager;

enum class ModelImportResult { NONE, CANCELLED, ACCEPTED };

/// <summary>
/// 外部モデルと依存元の基準、Assets内の新規フォルダを選択する
/// </summary>
class ModelImportDialog {
  public:
	/// <summary>
	/// 現在のAssetsフォルダを作成先の初期値として、取り込み画面の表示を予約する
	/// </summary>
	/// <param name="assets">検索先と既存フォルダの一覧</param>
	/// <param name="folder">検索先基準の作成先。空は検索先自身</param>
	void Show(const AssetDatabase &assets, const std::filesystem::path &folder);

	/// <summary>
	/// モデルの選択と、新規フォルダへの依存ファイル一式の取り込みを表示する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="models">取り込み先のGPU読み込みを確認するモデル管理</param>
	/// <returns>入力中・取消・取り込み完了。失敗時は画面と入力を保持する</returns>
	ModelImportResult Draw(const ImGuiSystem &ui, ModelManager &models);

	/// <summary>
	/// 取り込んだモデルの参照を取得する
	/// </summary>
	/// <returns>ACCEPTED後に使用できるScene用のモデルパス。検索先が絶対指定なら絶対参照</returns>
	const std::filesystem::path &GetImportedPath() const;

  private:
	ModelImportOptions options_;
	FileDialog picker_;
	std::vector<std::filesystem::path> folders_;
	std::filesystem::path imported_;
	std::string name_, error_;
	bool opening_ = false, selecting_ = false;
};
