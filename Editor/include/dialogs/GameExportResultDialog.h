#pragma once

#include "platform/windows/ShellUtils.h"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

class ImGuiSystem;
struct GameConfig;

enum class GameExportResultAction { NONE, CLOSE, NEW_EXPORT };

/// <summary>
/// ゲーム出力の完了後に、出力フォルダ・単独起動・起動ログの確認を表示する
/// </summary>
class GameExportResultDialog {
  public:
	/// <summary>
	/// 成功した出力先を保持し、完了画面の表示を予約する
	/// </summary>
	/// <param name="directory">出力済みパッケージの絶対パス</param>
	/// <param name="reopening">前回の出力として開くか</param>
	void Show(std::filesystem::path directory, bool reopening = false);

	/// <summary>
	/// 現在の配布版とログの表示位置を保持したまま結果画面に戻る
	/// </summary>
	void Reopen() { opening_ = true; }

	/// <summary>
	/// 表示中の配布版のフォルダを取得する
	/// </summary>
	const std::filesystem::path &GetDirectory() const { return directory_; }

	/// <summary>
	/// 出力先を表示し、開く・起動・閉じる操作を受け付ける。失敗時は画面を保持する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <returns>閉じるか新しい出力へ進む要求。開く・起動操作の後はNONE</returns>
	GameExportResultAction Draw(const ImGuiSystem &ui);

  private:
	struct LogPosition {
		std::vector<std::uintmax_t> earlierPages;
		std::uintmax_t offset;
		bool paged;
		bool follow;
		bool errorsOnly;
		bool sinceLaunchOnly;
		float scrollY;
		std::string filter;
	};

	struct GameRun {
		struct LogMatch {
			std::string query;
			std::uintmax_t offset;
			std::uintmax_t entryOffset;
			bool errorsOnly;
			bool inError;
		};
		ShellUtils::ExecutableProcess process;
		std::string status;
		std::string launchSettings;
		std::string crashError;
		std::filesystem::path launchFailurePath;
		std::optional<std::uintmax_t> logStart;
		std::vector<std::uintmax_t> earlierLogPages;
		std::optional<LogPosition> beforeErrorJump;
		std::uintmax_t logPageOffset = 0;
		float logScrollY = 0.0f;
		std::string logFilter;
		std::optional<LogMatch> lastMatch;
		bool sinceLaunchOnly = false;
		bool showLog = false;
		bool followLog = true;
		bool showErrorsOnly = false;
		bool showLogBeginning = false;
		bool launchFailed = false;
		bool restartRequested = false;
	};

	/// <summary>
	/// ゲームを起動し、今回のログの開始位置を記録する。ログを更新した場合はtrue
	/// </summary>
	bool StartGame();

	/// <summary>
	/// 配布版の画面設定と起動シーンを読み直し、表示内容と読み込みエラーを更新する
	/// </summary>
	void ReloadGameSettings(GameConfig &config);

	/// <summary>
	/// 出力したゲームのログの指定ページまたは末尾を読み直す
	/// </summary>
	void RefreshLog();

	enum class ErrorSearch { FIRST, PREVIOUS, NEXT, LATEST, CRASH_CAUSE };

	/// <summary>
	/// 選択中の実行範囲で、指定方向のエラーから表示する
	/// </summary>
	bool OpenLogAtError(ErrorSearch search, bool reportMissing = true);

	enum class MatchSearch { FIRST, PREVIOUS, NEXT };

	/// <summary>
	/// 選択中の実行範囲で最初の一致行、または前後の一致行へ移動する
	/// </summary>
	bool FindLogMatch(MatchSearch direction);

	/// <summary>
	/// 元の閲覧位置を記録してから、指定のオフセットでページを開く
	/// </summary>
	void OpenLogAtOffset(std::uintmax_t offset);

	/// <summary>
	/// 起動したゲームの終了を確認する。異常終了した場合はログを開く
	/// </summary>
	bool UpdateGameProcess(bool &jumpToBeginning, bool &jumpToLatest);

	std::filesystem::path directory_;
	std::map<std::filesystem::path, GameRun> runs_;
	std::string error_;
	std::string packageSettings_;
	std::string packageSettingsError_;
	std::string log_, logError_;
	std::chrono::steady_clock::time_point nextLogRefresh_;
	bool opening_ = false;
	bool reopening_ = false;
	bool logTruncated_ = false;
	std::optional<std::uintmax_t> nextLogPage_;
};
