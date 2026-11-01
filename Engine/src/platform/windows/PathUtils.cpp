#include "platform/windows/PathUtils.h"
#include "platform/windows/StringConversion.h"

#include <Windows.h>
#include <stdexcept>
#include <string>

std::filesystem::path PathUtils::GetExecutablePath() {
	std::wstring buffer(32768, L'\0');

	const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
	                                        static_cast<DWORD>(buffer.size()));

	if (length == 0 || length >= buffer.size()) {
		return {};
	}

	buffer.resize(length);

	return std::filesystem::path(buffer);
}

std::filesystem::path PathUtils::GetExecutableDirectory() {
	return GetExecutablePath().parent_path();
}

std::filesystem::path
PathUtils::ResolveFromExecutable(const std::filesystem::path &path) {
	if (path.empty()) {
		throw std::invalid_argument("ファイルのパスが空です。");
	}

	if (path.is_absolute()) {
		return path.lexically_normal();
	}

	if (path.has_root_path()) {
		throw std::invalid_argument(
		    "ドライブやルートに対する相対パスは使用できません。");
	}

	const auto directory = GetExecutableDirectory();

	if (directory.empty()) {
		throw std::runtime_error(
		    "実行ファイルのフォルダを取得できませんでした。");
	}

	return (directory / path).lexically_normal();
}

std::string PathUtils::ToUtf8(const std::filesystem::path &path) {
	const auto text = path.generic_u8string();
	return std::string(reinterpret_cast<const char *>(text.data()),
	                   text.size());
}

bool PathUtils::IsSameFile(const std::filesystem::path &first,
                           const std::filesystem::path &second) {
	auto firstPath = ResolveFromExecutable(first);
	auto secondPath = ResolveFromExecutable(second);
	firstPath.make_preferred();
	secondPath.make_preferred();

	if (CompareStringOrdinal(firstPath.c_str(), -1, secondPath.c_str(), -1,
	                         TRUE) == CSTR_EQUAL) {
		return true;
	}

	std::error_code error;
	return std::filesystem::equivalent(firstPath, secondPath, error);
}

std::filesystem::path PathUtils::FromUtf8(std::string_view text) {
	if (text.empty()) {
		return {};
	}

	const auto wide = StringConversion::Utf8ToWide(text);

	if (wide.empty() || text.find('\0') != std::string_view::npos) {
		throw std::invalid_argument("ファイルパスのUTF-8文字列が無効です。");
	}

	return std::filesystem::path(wide);
}

bool PathUtils::HasExtension(const std::filesystem::path &path,
                             std::string_view extension) {
	const std::string actual = ToUtf8(path.extension());

	if (actual.size() != extension.size()) {
		return false;
	}

	for (std::size_t i = 0; i < actual.size(); ++i) {
		const auto toLowerAscii = [](unsigned char value) {
			if (value >= 'A' && value <= 'Z') {
				return static_cast<unsigned char>(value + ('a' - 'A'));
			}

			return value;
		};

		if (toLowerAscii(static_cast<unsigned char>(actual[i])) !=
		    toLowerAscii(static_cast<unsigned char>(extension[i]))) {
			return false;
		}
	}

	return true;
}
