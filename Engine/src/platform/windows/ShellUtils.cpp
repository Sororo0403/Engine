#include "platform/windows/ShellUtils.h"
#include "platform/windows/PathUtils.h"

#include <Windows.h>
#include <format>
#include <shellapi.h>
#include <stdexcept>

namespace {
/// <summary>
/// シェルへ渡す参照を、存在する絶対パスへ揃える
/// </summary>
/// <param name="path">絶対パスまたは実行ファイル基準のパス</param>
/// <returns>正規化した絶対パス。空・NUL・ドライブ相対指定や欠損は例外</returns>
std::filesystem::path Resolve(const std::filesystem::path &path) {
	if (path.empty() || path.native().find(L'\0') != std::wstring::npos ||
	    (path.has_root_path() && !path.is_absolute())) {
		throw std::invalid_argument("Choose a valid local path.");
	}
	return std::filesystem::canonical(PathUtils::ResolveFromExecutable(path));
}

/// <summary>
/// パスをコマンド文字列へ変換せず、開く操作をWindowsへ渡す
/// </summary>
/// <param name="path">検証済みの絶対パス</param>
/// <param name="directory">起動先の作業ディレクトリ</param>
/// <param name="visibility">起動先へ指定する初期表示</param>
void Open(const std::filesystem::path &path,
          const std::filesystem::path &directory,
          ShellUtils::WindowVisibility visibility) {
	SHELLEXECUTEINFOW info{};
	info.cbSize = sizeof(info);
	info.fMask =
	    SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOCLOSEPROCESS;
	info.lpVerb = L"open";
	info.lpFile = path.c_str();
	info.lpDirectory = directory.c_str();
	info.nShow = visibility == ShellUtils::WindowVisibility::HIDDEN
	                 ? SW_HIDE
	                 : SW_SHOWNORMAL;
	if (!ShellExecuteExW(&info)) {
		const auto code = GetLastError();
		throw std::runtime_error(
		    std::format("Could not open {} (Windows error {}).",
		                PathUtils::ToUtf8(path), code));
	}
	if (info.hProcess) {
		CloseHandle(info.hProcess);
	}
}
} // namespace

bool ShellUtils::OpenDirectory(const std::filesystem::path &path,
                               std::string &error) {
	try {
		const auto resolved = Resolve(path);
		if (!std::filesystem::is_directory(resolved)) {
			throw std::invalid_argument("Choose an existing folder.");
		}
		Open(resolved, resolved, WindowVisibility::NORMAL);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool ShellUtils::RunExecutable(const std::filesystem::path &path,
                               std::string &error,
                               WindowVisibility visibility) {
	try {
		const auto resolved = Resolve(path);
		if (!PathUtils::HasExtension(resolved, ".exe") ||
		    !std::filesystem::is_regular_file(resolved)) {
			throw std::invalid_argument("Choose an existing .exe file.");
		}
		Open(resolved, resolved.parent_path(), visibility);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
