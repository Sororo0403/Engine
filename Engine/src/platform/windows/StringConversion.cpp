#include "platform/windows/StringConversion.h"

#include <Windows.h>
#include <climits>

std::wstring StringConversion::Utf8ToWide(std::string_view text) {
	if (text.empty() ||
		text.size() > static_cast<size_t>(INT_MAX)) {
		return {};
	}

	const int length = static_cast<int>(text.size());

	// 変換先に必要な文字数を調べる
	const int size = MultiByteToWideChar(
		CP_UTF8,
		MB_ERR_INVALID_CHARS,
		text.data(),
		length,
		nullptr,
		0
	);

	if (size == 0) {
		return {};
	}

	std::wstring result(static_cast<size_t>(size), L'\0');

	const int converted = MultiByteToWideChar(
		CP_UTF8,
		MB_ERR_INVALID_CHARS,
		text.data(),
		length,
		result.data(),
		size
	);

	if (converted == 0) {
		return {};
	}

	return result;
}

std::string StringConversion::WideToUtf8(std::wstring_view text) {
	if (text.empty() ||
		text.size() > static_cast<size_t>(INT_MAX)) {
		return {};
	}

	const int length = static_cast<int>(text.size());

	// 変換先に必要なバイト数を調べる
	const int size = WideCharToMultiByte(
		CP_UTF8,
		WC_ERR_INVALID_CHARS,
		text.data(),
		length,
		nullptr,
		0,
		nullptr,
		nullptr
	);

	if (size == 0) {
		return {};
	}

	std::string result(static_cast<size_t>(size), '\0');

	const int converted = WideCharToMultiByte(
		CP_UTF8,
		WC_ERR_INVALID_CHARS,
		text.data(),
		length,
		result.data(),
		size,
		nullptr,
		nullptr
	);

	if (converted == 0) {
		return {};
	}

	return result;
}
