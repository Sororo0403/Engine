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

std::filesystem::path PathUtils::DecodeRelativeUri(std::string_view uri) {
	const auto hex = [](char value) -> int {
		if (value >= '0' && value <= '9') {
			return value - '0';
		}

		if (value >= 'a' && value <= 'f') {
			return value - 'a' + 10;
		}

		if (value >= 'A' && value <= 'F') {
			return value - 'A' + 10;
		}

		return -1;
	};
	std::string decoded;
	decoded.reserve(uri.size());

	for (std::size_t i = 0; i < uri.size(); ++i) {
		if (uri[i] != '%') {
			decoded += uri[i];
			continue;
		}

		if (i + 2 >= uri.size() || hex(uri[i + 1]) < 0 || hex(uri[i + 2]) < 0) {
			throw std::invalid_argument("相対URIのパーセント表記が無効です。");
		}

		decoded += static_cast<char>(hex(uri[i + 1]) * 16 + hex(uri[i + 2]));
		i += 2;
	}

	const auto path = FromUtf8(decoded);

	if (path.empty() || path.has_root_path() ||
	    decoded.find(':') != std::string::npos ||
	    decoded.find('\0') != std::string::npos) {
		throw std::invalid_argument(
		    "URIにはローカルの相対パスを指定してください。");
	}

	return path;
}

bool PathUtils::IsImageFile(const std::filesystem::path &path) {
	for (const auto extension :
	     {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".gif"}) {
		if (HasExtension(path, extension)) {
			return true;
		}
	}

	return false;
}

bool PathUtils::IsValidFileName(const std::filesystem::path &name) {
	const auto &text = name.native();
	if (text.empty() || text.size() > 255 || name.has_root_path() ||
	    name.has_parent_path() || text.back() == L'.' || text.back() == L' ' ||
	    text.find_first_of(L"<>:\"/\\|?*") != std::wstring::npos) {
		return false;
	}

	for (const auto character : text) {
		if (character < 32) {
			return false;
		}
	}

	auto base = text.substr(0, text.find(L'.'));
	while (!base.empty() && base.back() == L' ') {
		base.pop_back();
	}

	for (auto &character : base) {
		if (character >= L'a' && character <= L'z') {
			character -= L'a' - L'A';
		}
	}

	if (base == L"CON" || base == L"PRN" || base == L"AUX" || base == L"NUL") {
		return false;
	}

	if (base.size() == 4 &&
	    (base.starts_with(L"COM") || base.starts_with(L"LPT"))) {
		const auto digit = base.back();
		if ((digit >= L'1' && digit <= L'9') || digit == L'\u00b9' ||
		    digit == L'\u00b2' || digit == L'\u00b3') {
			return false;
		}
	}

	return true;
}

std::filesystem::path
PathUtils::RelativeInside(const std::filesystem::path &root,
                          const std::filesystem::path &path) {
	if (root.empty() || path.empty() ||
	    root.native().find(L'\0') != std::wstring::npos ||
	    path.native().find(L'\0') != std::wstring::npos) {
		throw std::invalid_argument(
		    "Root and target paths must be nonempty and valid.");
	}

	auto a = root.begin(), b = path.begin();

	for (; a != root.end(); ++a, ++b) {
		if (b == path.end() || CompareStringOrdinal(a->c_str(), -1, b->c_str(),
		                                            -1, TRUE) != CSTR_EQUAL) {
			throw std::runtime_error("A path is outside the selected root: " +
			                         PathUtils::ToUtf8(path));
		}
	}

	std::filesystem::path relative;

	for (; b != path.end(); ++b) {
		relative /= *b;
	}

	return relative;
}

bool PathUtils::IsModelFile(const std::filesystem::path &path) {
	return HasExtension(path, ".glb") || HasExtension(path, ".gltf") ||
	       HasExtension(path, ".obj");
}
