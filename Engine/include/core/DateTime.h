#pragma once

#include <string>

namespace DateTime {
	/// <summary>
	/// 現在のローカル時刻を文字列として取得する
	/// </summary>
	/// <returns>HH:MM:SS形式の時刻。取得失敗時は空文字列</returns>
	std::string GetCurrentTimeString();
}
