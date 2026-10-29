#include "core/Logger.h"

#include "core/DateTime.h"
#include "platform/windows/StringConversion.h"

#include <Windows.h>

#include <cerrno>
#include <filesystem>
#include <format>
#include <sstream>

bool Logger::Initialize(const std::string &filePath) {
	if (logFile_.is_open() || filePath.empty()) {
		Error("Logger::Initialize: 初期化済み、または保存先パスが空です。");
		return false;
	}

	const std::wstring widePath = StringConversion::Utf8ToWide(filePath);

	if (widePath.empty()) {
		Error("Logger::Initialize: 保存先パスをUTF-16に変換できませんでした。");
		return false;
	}

	const std::filesystem::path path(widePath);

	// 保存先のフォルダがなければ作成する
	if (path.has_parent_path()) {
		std::error_code error;

		std::filesystem::create_directories(path.parent_path(), error);

		if (error) {
			Error(
			    std::format("ログフォルダを作成できませんでした: {} (code={})",
			                filePath, error.value()));
			return false;
		}
	}

	logFile_.clear();

	// 既存のログの末尾に追記する
	logFile_.open(path, std::ios::out | std::ios::app);

	if (!logFile_.is_open()) {
		const int error = errno;
		Error(std::format("ログファイルを開けませんでした: {} (errno={})",
		                  filePath, error));
		return false;
	}

	return true;
}

void Logger::Finalize() {
	if (logFile_.is_open()) {
		logFile_.close();
	}
}

void Logger::Info(std::string_view message) { Write(Level::LOG_INFO, message); }

void Logger::Warning(std::string_view message) {
	Write(Level::LOG_WARNING, message);
}

void Logger::Error(std::string_view message) {
	Write(Level::LOG_ERROR, message);
}

void Logger::Write(Level level, std::string_view message) {
	const char *levelName = "";

	switch (level) {
	case Level::LOG_INFO:
		levelName = "INFO";
		break;
	case Level::LOG_WARNING:
		levelName = "WARNING";
		break;
	case Level::LOG_ERROR:
		levelName = "ERROR";
		break;
	}

	// 時刻・レベル・メッセージを1行にまとめる
	std::ostringstream stream;
	stream << '[' << DateTime::GetCurrentTimeString() << "] [" << levelName
	       << "] " << message << '\n';

	const std::string line = stream.str();

	// Visual Studioの出力ウィンドウへ出力する
	const std::wstring wideLine = StringConversion::Utf8ToWide(line);

	if (!wideLine.empty()) {
		OutputDebugStringW(wideLine.c_str());
	}

	// ファイルへUTF-8で書き込む
	if (logFile_.is_open()) {
		logFile_ << line;
		logFile_.flush();
	}
}
