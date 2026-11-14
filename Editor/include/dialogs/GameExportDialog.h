#pragma once

#include "dialogs/FileDialog.h"
#include "export/GameExporter.h"
#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>

class ImGuiSystem;

enum class GameExportResult {
	NONE,
	CANCELLED,
	CHECK,
	SAVE_AND_CHECK,
	ACCEPTED,
	CHOOSE_STARTUP_SCENE,
	OPEN_ISSUE,
	OPEN_TRANSITION_SOURCE,
	REVEAL_ISSUE_ASSET,
	EDIT_ISSUE_SCRIPT,
	EDIT_ISSUE_INPUT
};

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
	/// <param name="projectRoot">下書きの所属先。プロジェクトなしなら空</param>
	/// <param name="lastExport">同じプロジェクトの最後の出力先。未記録なら空</param>
	/// <param name="windowSource">次の出力に引き継ぐウィンドウ設定の取得元。通常は空</param>
	/// <returns>問題の修正から戻った場合はtrue。再検査できる設定を保持する</returns>
	bool Show(GameExportOptions options, bool includesUnsavedEdits,
	          const std::filesystem::path &projectRoot = {},
	          const std::filesystem::path &lastExport = {},
	          const std::filesystem::path &windowSource = {});

	/// <summary>
	/// 無題の起動シーンが置き換わるとき、その出力下書きを破棄する
	/// </summary>
	void ForgetUntitledDraft();

	/// <summary>
	/// プロジェクトを切り替え、保存済みシーンの配布下書きをプロジェクトごとに保持する
	/// </summary>
	void SwitchProject(const std::filesystem::path &projectRoot);

	/// <summary>
	/// 現在の保存済みシーンの下書きを取得する。保存対象がなければ空
	/// </summary>
	std::optional<GameExportDraft> GetDraft() const;

	/// <summary>
	/// プロジェクトを開いたとき、メモリ上に下書きがなければ設定から復元する
	/// </summary>
	void RestoreDraft(const GameExportDraft &draft);

	/// <summary>
	/// 編集中のシーンを別のパスへ保存したとき、その出力下書きを引き継ぐ
	/// </summary>
	void FollowSavedScene(const std::filesystem::path &projectRoot,
	                     const std::filesystem::path &previousPath,
	                     const std::filesystem::path &savedPath);

	/// <summary>
	/// 起動シーンを移動したとき、その出力下書きを移動先に引き継ぐ
	/// </summary>
	void FollowMovedScene(
	    const std::filesystem::path &projectRoot,
	    std::span<const std::pair<std::filesystem::path, std::filesystem::path>> moves);

	/// <summary>
	/// 出力設定の入力を保持したまま起動シーンの表示を更新する
	/// </summary>
	void UpdateStartupScene(const std::filesystem::path &path, bool includesUnsavedEdits);

	/// <summary>
	/// プロジェクトの起動シーンを変更したとき、同じ起動シーンの配布下書きを引き継ぐ
	/// </summary>
	void FollowStartupScene(const std::filesystem::path &projectRoot,
	                        const std::filesystem::path &previousPath,
	                        const std::filesystem::path &startupPath);

	/// <summary>
	/// 出力設定の入力を描画する
	/// </summary>
	/// <param name="ui">UI機能</param>
	/// <returns>確定・キャンセル・入力中</returns>
	/// <param name="canSaveAndCheck">別シーンの未保存変更を保存して再検査できるか</param>
	GameExportResult Draw(const ImGuiSystem &ui, bool canSaveAndCheck = false);

	/// <summary>
	/// 出力前検査の結果をダイアログに表示する
	/// </summary>
	/// <param name="continueFromIssue">修正した項目の付近から確認を続けるか</param>
	void SetCheckResult(std::string message, std::vector<GameExportIssue> issues = {},
	                    bool continueFromIssue = false);

	/// <summary>
	/// シーン・素材・スクリプトへの移動操作で選択した不備を取得する
	/// </summary>
	const GameExportIssue &GetSelectedIssue() const;

	/// <summary>
	/// 出力に失敗したとき、入力を保持したままダイアログを開き直す
	/// </summary>
	void ShowExportFailure(std::string error, std::vector<GameExportIssue> issues);

	/// <summary>
	/// 入力を保持したまま出力画面を開き直す
	/// </summary>
	/// <param name="error">移動や編集の失敗理由</param>
	void Reopen(std::string error = {});

	/// <summary>
	/// 次に同じ起動シーンの出力画面を開くとき、現在の入力を引き継ぐ
	/// </summary>
	void PreserveDraft();

	/// <summary>
	/// 確定した出力設定を取得する
	/// </summary>
	/// <returns>CHECKまたはACCEPTED後に使用する設定</returns>
	const GameExportOptions &GetOptions() const {
		return options_;
	}

  private:
	struct WindowSettings {
		std::string title;
		int width;
		int height;
	};
	struct Draft {
		std::filesystem::path startupScene;
		std::filesystem::path destination;
		std::string output;
		WindowSettings window;
		std::string checkResult;
		std::string issueFilter;
		std::vector<GameExportIssue> issues;
		std::optional<std::size_t> selectedIssue;
		bool recheck;
	};

	void ClearCheckResult();
	bool SuggestOutputName();

	GameExportOptions options_;
	FileDialog folderDialog_;
	std::filesystem::path defaultOutputName_;
	std::filesystem::path browseOutputName_;
	std::filesystem::path projectRoot_;
	std::optional<WindowSettings> lastSuccessfulWindow_;
	std::string output_, startupScene_, title_, error_;
	std::string checkResult_;
	bool checkResultStale_ = false;
	std::string issueFilter_;
	std::vector<GameExportIssue> checkIssues_;
	std::optional<std::size_t> selectedIssue_;
	int width_ = 1280, height_ = 720;
	bool opening_ = false;
	bool includesUnsavedEdits_ = false;
	bool browsing_ = false;
	bool recheckAfterBrowse_ = false;
	GameExportField focusField_ = GameExportField::NONE;
	std::optional<std::size_t> issueToScrollTo_;
	std::size_t issuePage_ = 0;
	bool resumeDraft_ = false;
	bool recheckRestoredDraft_ = false;
	std::map<std::filesystem::path, Draft> projectDrafts_;
};
