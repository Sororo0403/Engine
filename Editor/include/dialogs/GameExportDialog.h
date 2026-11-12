#pragma once

#include "dialogs/FileDialog.h"
#include "export/GameExporter.h"
#include <string>

class ImGuiSystem;

enum class GameExportResult { NONE, CANCELLED, ACCEPTED };

/// <summary>
/// ゲームの出力先・タイトル・画面サイズを選択する
/// </summary>
class GameExportDialog {
  public:
	/// <summary>
	/// 初期値を設定し、ダイアログを開く
	/// </summary>
	/// <param name="options">入力元・カメラ・初期の出力先</param>
	/// <param name="includesUnsavedEdits">編集中の起動シーンの未保存変更を出力に含むか</param>
	void Show(GameExportOptions options, bool includesUnsavedEdits);

	/// <summary>
	/// 出力設定の入力を描画する
	/// </summary>
	/// <param name="ui">UI機能</param>
	/// <returns>確定・キャンセル・入力中</returns>
	GameExportResult Draw(const ImGuiSystem &ui);

	/// <summary>
	/// 確定した出力設定を取得する
	/// </summary>
	/// <returns>ACCEPTED後に使用する設定</returns>
	const GameExportOptions &GetOptions() const {
		return options_;
	}

  private:
	GameExportOptions options_;
	FileDialog folderDialog_;
	std::string output_, startupScene_, title_, error_;
	int width_ = 1280, height_ = 720;
	bool opening_ = false;
	bool includesUnsavedEdits_ = false;
	bool browsing_ = false;
};
