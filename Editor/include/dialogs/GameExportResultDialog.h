#pragma once

#include <cstdint>
#include <filesystem>
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
	/// <summary>
	/// 出力したゲームのログを末尾から読み直す
	/// </summary>
	void RefreshLog();

	std::filesystem::path directory_;
	std::string error_;
	std::string log_, logError_;
	std::optional<std::uintmax_t> logLaunchStart_;
	bool opening_ = false;
	bool reopening_ = false;
	bool showLog_ = false;
	bool logTruncated_ = false;
	bool sinceLaunchOnly_ = false;
};
