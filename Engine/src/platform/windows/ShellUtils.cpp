#include "platform/windows/ShellUtils.h"
#include "platform/windows/PathUtils.h"

#include <Windows.h>
#include <format>
#include <shellapi.h>
#include <shlobj.h>
#include <stdexcept>
#include <wrl/client.h>

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

bool ShellUtils::RecycleAsset(const std::filesystem::path &path,
                             std::string &error) {
	try {
		const auto source = Resolve(path);
		const auto attributes = GetFileAttributesW(path.c_str());

		if (source != path || attributes == INVALID_FILE_ATTRIBUTES ||
		    (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
			throw std::invalid_argument("Choose a verified file or folder.");
		}

		const auto check = [](HRESULT result) {
			if (FAILED(result)) {
				throw std::runtime_error(std::format(
				    "Could not move the item to the Recycle Bin (0x{:08X}).",
				    static_cast<unsigned long>(result)));
			}
		};

		using Microsoft::WRL::ComPtr;
		ComPtr<IShellItem> item, parent, bin, recycled;
		ComPtr<ITransferSource> transfer;
		check(SHCreateItemFromParsingName(source.c_str(), nullptr,
		                                 IID_PPV_ARGS(&item)));
		check(item->GetParent(&parent));
		check(parent->BindToHandler(nullptr, BHID_Transfer,
		                            IID_PPV_ARGS(&transfer)));
		check(SHGetKnownFolderItem(FOLDERID_RecycleBinFolder, KF_FLAG_DEFAULT,
		                           nullptr, IID_PPV_ARGS(&bin)));
		check(transfer->RecycleItem(item.Get(), bin.Get(), TSF_NORMAL, &recycled));

		if (!recycled) {
			throw std::runtime_error("Windows did not recycle the item.");
		}

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
