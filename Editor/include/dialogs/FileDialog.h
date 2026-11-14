#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
enum class FileDialogFilter { SCENE, IMAGE, MODEL, GAME_CONFIG, AUDIO, PROJECT, GAME_EXPORT };

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
	/// <param name="filter">一覧と確定時のファイル種別。IMAGE・MODEL・AUDIOはOPENのみ、GAME_CONFIGはファイルの保存にも対応する</param>
	/// <param name="title">用途を示す任意のダイアログ名。空なら種別に応じた名前</param>
	void Show(FileDialogMode mode,
	          const std::filesystem::path &initialPath = {},
	          FileDialogFilter filter = FileDialogFilter::SCENE,
	          std::string title = {});

	/// <summary>
	/// インポート元を選ぶ。指定がなければ、このダイアログで前回使ったフォルダを開く
	/// </summary>
	/// <param name="source">選択し直す場合の現在のファイル。初回の選択では空</param>
	/// <param name="filter">取り込めるファイルの種類</param>
	void ShowImportSource(const std::filesystem::path &source,
	                      FileDialogFilter filter);

	/// <summary>
	/// 保存先に未使用のファイル名を要求する。Showのあとに指定する
	/// </summary>
	void RequireNewFile() { requireNewFile_ = true; }

	/// <summary>
	/// 開くダイアログで検索できるプロジェクト内のシーン一覧を設定する
	/// </summary>
	void SetProjectScenes(const std::filesystem::path &root,
	                      std::span<const std::filesystem::path> paths,
	                      bool searchProject = false);

	/// <summary>
	/// 検索範囲と入力内容を保ったまま、プロジェクトのシーン一覧を差し替える
	/// </summary>
	void UpdateProjectScenes(std::span<const std::filesystem::path> paths);

	/// <summary>
	/// 前回の一覧を保持し、プロジェクトのシーン一覧を更新できなかった理由を表示する
	/// </summary>
	void SetProjectSceneRefreshError(std::string error);

	/// <summary>
	/// プロジェクトのシーン一覧の更新要求を取得し、消費する
	/// </summary>
	bool TakeProjectSceneRefreshRequest();

	/// <summary>
	/// 検索範囲のチェックボックスで選んだ値を取得し、変更通知を消費する
	/// </summary>
	std::optional<bool> TakeProjectSearchChange();

	/// <summary>
	/// 保存確認から選択画面へ戻り、プロジェクトの再試行情報を引き継ぐ
	/// </summary>
	void RetrySelection();

	/// <summary>
	/// 選択したファイルやフォルダを使えなかったとき、同じ場所から選び直す
	/// </summary>
	/// <param name="error">選択画面に表示するエラー。選び直すだけなら空</param>
	/// <param name="failedLocation">確認に失敗したファイルまたはフォルダ。空なら場所を表示しない</param>
	void RetrySelection(std::string error,
	                    std::filesystem::path failedLocation = {});

	/// <summary>
	/// 移動したプロジェクトを選ぶときに、以前の作業状態を復元する選択肢を表示する
	/// </summary>
	void OfferPreviousProjectSession() { offerPreviousProjectSession_ = true; }

	/// <summary>
	/// 以前のプロジェクトの作業状態を復元するか取得する
	/// </summary>
	bool ShouldRestorePreviousProjectSession() const { return restorePreviousProjectSession_; }

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

	/// <summary>
	/// 選択した保存先の上書きを確認したかを返す
	/// </summary>
	bool IsOverwriteConfirmed() const { return overwriteConfirmed_; }

	/// <summary>
	/// 配布版に必要なフォルダとファイルを確認し、開けない理由を返す
	/// </summary>
	static std::string_view GameExportOpenProblem(const std::filesystem::path &directory);

  private:
	/// <summary>
	/// 一覧に表示するフォルダまたはファイル
	/// </summary>
	struct Entry {
		std::filesystem::path path;
		bool directory = false;
		bool openableFolder = false;
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
	/// Folder欄とファイル名を検証し、入力したフォルダを基準に選択を確定する
	/// </summary>
	/// <returns>true:確定 false:入力エラー、または上書き確認が必要</returns>
	bool AcceptSelection();

	/// <summary>
	/// 入力したファイル名とフォルダからパスを組み立てる。ファイルの存在は確認しない
	/// </summary>
	std::filesystem::path ReadFilePath() const;

	/// <summary>
	/// Folder欄の絶対パスを確認する。ファイル・フォルダの選択と新規フォルダ作成に共通の基準を使う
	/// </summary>
	/// <returns>存在するフォルダの正規パス。無効な指定は例外</returns>
	std::filesystem::path ReadDirectory() const;

	/// <summary>
	/// フォルダ欄を読み取る。プロジェクトと配布版では指定されたファイルの親も受け付ける
	/// </summary>
	std::filesystem::path ReadFolderInput() const;

	/// <summary>
	/// Folder欄の下に単独の新規フォルダを作り、表示先を切り替える。既存項目は保持する
	/// </summary>
	void CreateFolder();

  private:
	FileDialogMode mode_ = FileDialogMode::OPEN;
	FileDialogFilter filter_ = FileDialogFilter::SCENE;

	std::filesystem::path directory_;
	std::filesystem::path selectedPath_;
	std::filesystem::path failedProject_;
	bool offerPreviousProjectSession_ = false;
	bool restorePreviousProjectSession_ = false;

	std::string directoryText_;
	std::string search_;
	std::string fileName_;
	std::string folderName_;
	std::string error_;
	std::string failedProjectLocationError_;
	std::string projectSceneRefreshError_;
	std::string title_;

	std::vector<Entry> entries_;
	std::vector<Entry> projectScenes_;
	std::filesystem::path projectRoot_;

	bool openRequested_ = false;
	bool destinationExists_ = false;
	bool requireNewFile_ = false;
	bool overwriteConfirmed_ = false;
	bool searchProjectScenes_ = false;
	bool projectSceneRefreshRequested_ = false;
	std::optional<bool> projectSearchChange_;
};
