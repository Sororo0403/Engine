#pragma once

#include <filesystem>
#include <string>
#include <vector>

class ImGuiSystem;

/// <summary>
/// ファイル選択ダイアログの用途
/// </summary>
enum class SceneFileDialogMode {
	OPEN,
	SAVE,
};

/// <summary>
/// ファイル選択ダイアログの結果
/// </summary>
enum class SceneFileDialogResult {
	NONE,
	ACCEPTED,
	CANCELLED,
};

/// <summary>
/// シーンファイルの選択と保存先の指定を行う
/// </summary>
class SceneFileDialog {
  public:
	/// <summary>
	/// ファイル選択の表示を予約する
	/// </summary>
	/// <param name="mode">開く、または保存</param>
	/// <param name="initialPath">初期ファイルまたはフォルダ。空の場合は実行ファイルのフォルダ</param>
	void Show(SceneFileDialogMode mode,
	          const std::filesystem::path &initialPath = {});

	/// <summary>
	/// ファイル選択を描画する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <returns>選択結果。未確定の場合はNONE</returns>
	SceneFileDialogResult Draw(const ImGuiSystem &ui);

	/// <summary>
	/// 確定したファイルパスを取得する
	/// </summary>
	/// <returns>ACCEPTEDが返された場合に使用できる絶対パス</returns>
	const std::filesystem::path &GetSelectedPath() const;

  private:
	/// <summary>
	/// 一覧に表示するフォルダまたはファイル
	/// </summary>
	struct Entry {
		std::filesystem::path path;
		bool directory = false;
	};

	/// <summary>
	/// 現在のフォルダの一覧を読み直す
	/// </summary>
	void Refresh();

	/// <summary>
	/// 表示するフォルダを変更する
	/// </summary>
	/// <param name="path">移動先の絶対パス</param>
	void ChangeDirectory(const std::filesystem::path &path);

	/// <summary>
	/// 入力されたファイル名を検証し、選択を確定する
	/// </summary>
	/// <returns>true:確定 false:入力エラー、または上書き確認が必要</returns>
	bool AcceptSelection();

  private:
	SceneFileDialogMode mode_ = SceneFileDialogMode::OPEN;

	std::filesystem::path directory_;
	std::filesystem::path selectedPath_;

	std::string directoryText_;
	std::string fileName_;
	std::string error_;

	std::vector<Entry> entries_;

	bool openRequested_ = false;
	bool confirmOverwrite_ = false;
};
