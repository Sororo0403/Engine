#pragma once

#include "assets/AudioImporter.h"
#include "dialogs/FileDialog.h"
#include <vector>
class AssetDatabase;
class ImGuiSystem;

enum class AudioImportResult { NONE, CANCELLED, ACCEPTED };

/// <summary>
/// 外部WAV音声の選択と、Assets内の取り込み先・ファイル名の指定を行う
/// </summary>
class AudioImportDialog {
  public:
	/// <summary>
	/// 現在のAssetsフォルダを初期値として、取り込み画面の表示を予約する
	/// </summary>
	/// <param name="assets">検索先と既存フォルダの一覧</param>
	/// <param name="folder">検索先を基準にした初期フォルダ。空は検索先自身</param>
	void Show(const AssetDatabase &assets, const std::filesystem::path &folder);

	/// <summary>
	/// WAV音声選択と取り込みを表示する。失敗時は画面と入力値を保持する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <returns>入力中・取消・取り込み完了</returns>
	AudioImportResult Draw(const ImGuiSystem &ui);

	/// <summary>
	/// 最後に取り込んだWAV音声の参照を取得する
	/// </summary>
	/// <returns>ACCEPTED後に使用できるScene用のWAV音声パス。検索先が絶対指定なら絶対参照</returns>
	const std::filesystem::path &GetImportedPath() const;

  private:
	AudioImportOptions options_;
	FileDialog picker_;
	std::vector<std::filesystem::path> folders_;
	std::filesystem::path imported_;
	std::string name_, error_;
	bool opening_ = false, selecting_ = false;
};
