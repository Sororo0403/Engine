#include "core/Logger.h"

#include "core/DateTime.h"
#include "platform/windows/StringConversion.h"

#include <Windows.h>

#include <cerrno>
#include <filesystem>
#include <format>
#include <sstream>

bool Logger::Initialize(const std::string &filePath) {
	const std::scoped_lock lock(mutex_);

	if (logFile_.is_open() || filePath.empty()) {
		Write(LogLevel::LOG_ERROR,
		      "Logger::Initialize: 初期化済み、または保存先パスが空です。");
		return false;
	}

	const std::wstring widePath = StringConversion::Utf8ToWide(filePath);

	if (widePath.empty()) {
		Write(LogLevel::LOG_ERROR,
		      "Logger::Initialize: 保存先パスをUTF-16に変換できませんでした。");
		return false;
	}

	const std::filesystem::path path(widePath);

	// 保存先のフォルダがなければ作成する
	if (path.has_parent_path()) {
		std::error_code error;

		std::filesystem::create_directories(path.parent_path(), error);

		if (error) {
			Write(
			    LogLevel::LOG_ERROR,
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
		Write(LogLevel::LOG_ERROR,
		      std::format("ログファイルを開けませんでした: {} (errno={})",
		                  filePath, error));
		return false;
	}

	return true;
}

void Logger::Finalize() {
	const std::scoped_lock lock(mutex_);

	if (logFile_.is_open()) {
		logFile_.close();
	}
}

void Logger::Info(std::string_view message) {
	Info(message, {});
}

void Logger::Info(std::string_view message, const std::filesystem::path &sourcePath) {
	const std::scoped_lock lock(mutex_);
	Write(LogLevel::LOG_INFO, message, sourcePath);
}

void Logger::Warning(std::string_view message) {
	const std::scoped_lock lock(mutex_);
	Write(LogLevel::LOG_WARNING, message);
}

void Logger::Error(std::string_view message) {
	const std::scoped_lock lock(mutex_);
	Write(LogLevel::LOG_ERROR, message);
}

void Logger::Error(std::string_view message, const std::filesystem::path &sourcePath) {
	const std::scoped_lock lock(mutex_);
	Write(LogLevel::LOG_ERROR, message, sourcePath);
}

void Logger::Write(LogLevel level, std::string_view message,
                   const std::filesystem::path &sourcePath) {
	const char *levelName = "";

	switch (level) {
	case LogLevel::LOG_INFO:
		levelName = "INFO";
		break;
	case LogLevel::LOG_WARNING:
		levelName = "WARNING";
		break;
	case LogLevel::LOG_ERROR:
		levelName = "ERROR";
		break;
	}

	// 時刻・レベル・メッセージを1行にまとめる
	const auto timestamp = DateTime::GetCurrentTimeString();
	std::ostringstream stream;
	stream << '[' << timestamp << "] [" << levelName << "] " << message << '\n';

	const std::string line = stream.str();

	entries_.push_back(
	    LogEntry{revision_ + 1, level, timestamp, std::string(message), sourcePath});

	if (entries_.size() > kMaximumEntries) {
		entries_.pop_front();
	}

	++revision_;
	if (capturePlayError_ && level == LogLevel::LOG_ERROR) {
		latestPlayError_ = entries_.back();
		if (!firstPlayError_) {
			firstPlayError_ = entries_.back();
		}
	}

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

bool Logger::ReadEntries(std::uint64_t &revision,
                         std::vector<LogEntry> &entries) {
	const std::scoped_lock lock(mutex_);

	if (revision == revision_) {
		return false;
	}

	std::vector<LogEntry> snapshot(entries_.begin(), entries_.end());
	entries.swap(snapshot);
	revision = revision_;
	return true;
}

void Logger::ClearEntries() {
	const std::scoped_lock lock(mutex_);
	entries_.clear();
	firstPlayError_.reset();
	latestPlayError_.reset();
	++revision_;
}

std::uint64_t Logger::GetEntryBoundary() {
	const std::scoped_lock lock(mutex_);
	return revision_;
}

std::uint64_t Logger::BeginPlayLog() {
	const std::scoped_lock lock(mutex_);
	firstPlayError_.reset();
	latestPlayError_.reset();
	capturePlayError_ = true;
	return revision_;
}

std::optional<LogEntry> Logger::GetFirstPlayError() {
	const std::scoped_lock lock(mutex_);
	return firstPlayError_;
}

std::optional<LogEntry> Logger::GetLatestPlayError() {
	const std::scoped_lock lock(mutex_);
	return latestPlayError_;
}
