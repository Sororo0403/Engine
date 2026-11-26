#pragma once

#include "core/LogEntry.h"

#include <deque>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Logger {
  public:
	/// <summary>
	/// ログファイルを開く
	/// </summary>
	/// <param name="filePath">UTF-8の保存先パス</param>
	/// <returns>true:成功 false:失敗</returns>
	static bool Initialize(const std::string &filePath);

	/// <summary>
	/// ログファイルを閉じる
	/// </summary>
	static void Finalize();

	/// <summary>
	/// 通常の情報を出力する
	/// </summary>
	/// <param name="message">UTF-8のメッセージ</param>
	static void Info(std::string_view message);

	/// <summary>
	/// 通常の情報と、その出力元のファイルを直近ログに記録する
	/// </summary>
	static void Info(std::string_view message, const std::filesystem::path &sourcePath);

	/// <summary>
	/// 警告を出力する
	/// </summary>
	/// <param name="message">UTF-8のメッセージ</param>
	static void Warning(std::string_view message);

	/// <summary>
	/// 警告と、その原因を確認できるファイルを直近ログに記録する
	/// </summary>
	static void Warning(std::string_view message, const std::filesystem::path &sourcePath);

	/// <summary>
	/// エラーを出力する
	/// </summary>
	/// <param name="message">UTF-8のメッセージ</param>
	static void Error(std::string_view message);

	/// <summary>
	/// エラーと、その原因を確認できるファイルを直近ログに記録する
	/// </summary>
	/// <returns>記録したログの識別子</returns>
	static std::uint64_t Error(std::string_view message, const std::filesystem::path &sourcePath);

	/// <summary>
	/// 試遊の失敗を記録し、Consoleの保持上限とは独立して次の試遊開始まで保持する
	/// </summary>
	static std::uint64_t PlayFailure(std::string_view message,
	                                 const std::filesystem::path &sourcePath);
	static std::optional<LogEntry> GetPlayFailure();

	/// <summary>
	/// 直近最大1,000件のログを、変更がある場合だけ出力順にコピーする
	/// </summary>
	/// <param name="revision">前回取得した版。初回は0。取得成功後に現在の版へ更新する</param>
	/// <param name="entries">取得先。返されたログはLoggerから独立して保持できる</param>
	/// <returns>true:取得先を更新 false:変更なし。確保失敗時は両引数を変更せず例外を送出する</returns>
	static bool ReadEntries(std::uint64_t &revision,
	                        std::vector<LogEntry> &entries);

	/// <summary>
	/// 現在までのログと、この後のログを分ける識別子を取得する
	/// </summary>
	static std::uint64_t GetEntryBoundary();

	/// <summary>
	/// 試遊のエラーを記録し始め、開始時点のログ識別子を返す
	/// </summary>
	static std::uint64_t BeginPlayLog();

	/// <summary>
	/// 直近の試遊で最後に出たエラーを取得する。ログ一覧の保持上限とは独立する
	/// </summary>
	static std::optional<LogEntry> GetLatestPlayError();

	/// <summary>
	/// 参照用の直近ログをクリアする。ファイルの出力内容は保持する
	/// </summary>
	static void ClearEntries();

  private:
	/// <summary>
	/// 時刻とレベルを付けて出力し、直近のログへ追加する。呼び出し元でmutexを保持する
	/// </summary>
	/// <param name="level">ログレベル</param>
	/// <param name="message">UTF-8のメッセージ</param>
	static std::uint64_t Write(LogLevel level, std::string_view message,
	                           const std::filesystem::path &sourcePath = {});

  private:
	inline static std::ofstream logFile_;
	inline static std::mutex mutex_;
	inline static std::deque<LogEntry> entries_;
	inline static std::optional<LogEntry> latestPlayError_;
	inline static std::optional<LogEntry> playFailure_;
	inline static bool capturePlayError_ = false;
	inline static std::uint64_t revision_ = 1;
	static constexpr std::size_t kMaximumEntries = 1000;
};
