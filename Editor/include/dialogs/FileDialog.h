#pragma once

#include <filesystem>
#include <string>
#include <vector>

class ImGuiSystem;

/// <summary>
/// ファイル選択ダイアログの用途
/// </summary>
enum class FileDialogMode {
	OPEN,
	SAVE,
	SAVE_COPY,
	SELECT_DIRECTORY,
};

/// <summary>
/// ファイル選択ダイアログの結果
/// </summary>
enum class FileDialogResult {
	NONE,
	ACCEPTED,
	CANCELLED,
};

/// <summary>
/// ファイル一覧と確定時に許可する種類
/// </summary>
enum class FileDialogFilter { SCENE, IMAGE, MODEL, GAME_CONFIG };

/// <summary>
/// シーンの保存・読み込みと、素材・ゲーム起動設定・フォルダの選択を行う
/// </summary>
class FileDialog {
  public:
	/// <summary>
	/// ファイル選択の表示を予約する
	/// </summary>
	/// <param name="mode">開く、保存、コピーを保存、またはフォルダを選択</param>
	/// <param name="initialPath">初期ファイルまたはフォルダ。空の場合は実行ファイルのフォルダ</param>
	/// <param name="filter">一覧と確定時のファイル種別。IMAGE・MODELはOPENのみ、GAME_CONFIGはファイルの保存にも対応する</param>
	void Show(FileDialogMode mode,
	          const std::filesystem::path &initialPath = {},
	          FileDialogFilter filter = FileDialogFilter::SCENE);

	/// <summary>
	/// ファイル選択を描画する。Escapeで選択を取り消す
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <returns>選択結果。未確定の場合はNONE</returns>
	FileDialogResult Draw(const ImGuiSystem &ui);

	/// <summary>
	/// 確定したファイルまたはフォルダのパスを取得する
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
	/// 入力されたファイル名またはフォルダを検証し、選択を確定する
	/// </summary>
	/// <returns>true:確定 false:入力エラー、または上書き確認が必要</returns>
	bool AcceptSelection();

	/// <summary>
	/// Folder欄の絶対パスを確認する。選択と新規フォルダ作成に共通の基準を使う
	/// </summary>
	/// <returns>存在するフォルダの正規パス。無効な指定は例外</returns>
	std::filesystem::path ReadDirectory() const;

	/// <summary>
	/// Folder欄の下に単独の新規フォルダを作り、表示先を切り替える。既存項目は保持する
	/// </summary>
	void CreateFolder();

  private:
	FileDialogMode mode_ = FileDialogMode::OPEN;
	FileDialogFilter filter_ = FileDialogFilter::SCENE;

	std::filesystem::path directory_;
	std::filesystem::path selectedPath_;

	std::string directoryText_;
	std::string fileName_;
	std::string folderName_;
	std::string error_;

	std::vector<Entry> entries_;

	bool openRequested_ = false;
	bool confirmOverwrite_ = false;
};
