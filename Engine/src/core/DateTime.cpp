#include "core/DateTime.h"

#include <chrono>
#include <ctime>

std::string DateTime::GetCurrentTimeString() {
	const auto now = std::chrono::system_clock::now();
	const std::time_t time = std::chrono::system_clock::to_time_t(now);

	std::tm localTime{};

	if (localtime_s(&localTime, &time) != 0) {
		return {};
	}

	// 時刻8文字と終端文字の分
	char buffer[9]{};

	if (std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &localTime) == 0) {
		return {};
	}

	return std::string(buffer);
}
