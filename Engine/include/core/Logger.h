#pragma once

#include <fstream>
#include <string>
#include <string_view>

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
	/// 警告を出力する
	/// </summary>
	/// <param name="message">UTF-8のメッセージ</param>
	static void Warning(std::string_view message);

	/// <summary>
	/// エラーを出力する
	/// </summary>
	/// <param name="message">UTF-8のメッセージ</param>
	static void Error(std::string_view message);

  private:
	enum class Level { LOG_INFO, LOG_WARNING, LOG_ERROR };

	/// <summary>
	/// 時刻とレベルを付けて、出力ウィンドウとファイルに書き込む
	/// </summary>
	/// <param name="level">ログレベル</param>
	/// <param name="message">UTF-8のメッセージ</param>
	static void Write(Level level, std::string_view message);

  private:
	inline static std::ofstream logFile_;
};
