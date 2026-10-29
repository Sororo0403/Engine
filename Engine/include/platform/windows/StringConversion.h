#pragma once

#include <string>
#include <string_view>

namespace StringConversion {
	/// <summary>
	/// UTF-8をWindowsのワイド文字列へ変換する
	/// </summary>
	/// <param name="text">UTF-8の文字列</param>
	/// <returns>変換結果。入力が空、または変換失敗時は空文字列</returns>
	std::wstring Utf8ToWide(std::string_view text);

	/// <summary>
	/// Windowsのワイド文字列をUTF-8へ変換する
	/// </summary>
	/// <param name="text">UTF-16の文字列</param>
	/// <returns>変換結果。入力が空、または変換失敗時は空文字列</returns>
	std::string WideToUtf8(std::wstring_view text);
}
