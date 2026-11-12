#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

/// <summary>
/// ログの重要度
/// </summary>
enum class LogLevel {
	LOG_INFO,
	LOG_WARNING,
	LOG_ERROR,
};

/// <summary>
/// 直近のログに保持する、出力順の識別子・重要度・時刻・本文
/// </summary>
struct LogEntry {
	std::uint64_t id = 0;
	LogLevel level = LogLevel::LOG_INFO;
	std::string timestamp;
	std::string message;
	std::filesystem::path sourcePath;
};
