#pragma once

#include "platform/windows/ShellUtils.h"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

class ImGuiSystem;

enum class GameExportResultAction { NONE, CLOSE };

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
	/// 出力先を表示し、開く・起動・閉じる操作を受け付ける。失敗時は画面を保持する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <returns>閉じる要求。開く・起動操作の後はNONEで画面を保持する</returns>
	GameExportResultAction Draw(const ImGuiSystem &ui);

  private:
	struct GameRun {
		ShellUtils::ExecutableProcess process;
		std::string status;
		std::optional<std::uintmax_t> logStart;
		bool sinceLaunchOnly = false;
		bool showLog = false;
		bool followLog = true;
		bool showLogBeginning = false;
		bool launchFailed = false;
		bool restartRequested = false;
	};

	/// <summary>
	/// ゲームを起動し、今回のログの開始位置を記録する。ログを更新した場合はtrue
	/// </summary>
	bool StartGame();

	/// <summary>
	/// 出力したゲームのログの先頭または末尾を読み直す
	/// </summary>
	void RefreshLog();

	/// <summary>
	/// 起動したゲームの終了を確認する。異常終了した場合はログを開く
	/// </summary>
	bool UpdateGameProcess(bool &jumpToLatest);

	std::filesystem::path directory_;
	std::map<std::filesystem::path, GameRun> runs_;
	std::string error_;
	std::string log_, logError_;
	std::chrono::steady_clock::time_point nextLogRefresh_;
	bool opening_ = false;
	bool reopening_ = false;
	bool logTruncated_ = false;
};
