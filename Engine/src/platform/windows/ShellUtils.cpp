#include "platform/windows/ShellUtils.h"
#include "platform/windows/PathUtils.h"

#include <Windows.h>
#include <format>
#include <memory>
#include <shellapi.h>
#include <shlobj.h>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <wrl/client.h>

namespace {
struct ProcessWindow {
	DWORD processId;
	HWND window = nullptr;
};

/// <summary>
/// 指定プロセスの表示中のメインウィンドウを探す
/// </summary>
BOOL CALLBACK FindProcessWindow(HWND window, LPARAM parameter) {
	auto &target = *reinterpret_cast<ProcessWindow *>(parameter);
	DWORD processId = 0;
	GetWindowThreadProcessId(window, &processId);
	if (processId != target.processId || !IsWindowVisible(window) ||
	    GetWindow(window, GW_OWNER)) {
		return TRUE;
	}
	target.window = window;
	return FALSE;
}

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
/// <param name="arguments">実行ファイルへ渡すコマンドライン引数</param>
HANDLE Open(const std::filesystem::path &path,
            const std::filesystem::path &directory,
            ShellUtils::WindowVisibility visibility,
            const std::wstring &arguments = {}) {
	SHELLEXECUTEINFOW info{};
	info.cbSize = sizeof(info);
	info.fMask =
	    SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOCLOSEPROCESS;
	info.lpVerb = L"open";
	info.lpFile = path.c_str();
	info.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
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

	return info.hProcess;
}

HANDLE OpenExecutable(const std::filesystem::path &path,
                      ShellUtils::WindowVisibility visibility,
                      const std::wstring &arguments = {}) {
	const auto resolved = Resolve(path);
	if (!PathUtils::HasExtension(resolved, ".exe") ||
	    !std::filesystem::is_regular_file(resolved)) {
		throw std::invalid_argument("Choose an existing .exe file.");
	}
	return Open(resolved, resolved.parent_path(), visibility, arguments);
}
} // namespace

bool ShellUtils::OpenDirectory(const std::filesystem::path &path,
                               std::string &error) {
	try {
		const auto resolved = Resolve(path);

		if (!std::filesystem::is_directory(resolved)) {
			throw std::invalid_argument("Choose an existing folder.");
		}

		if (const auto process = Open(resolved, resolved, WindowVisibility::NORMAL))
			CloseHandle(process);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool ShellUtils::RevealInExplorer(const std::filesystem::path &path,
                                  std::string &error) {
	try {
		// 元の表示名を保つため、リンクがある場合もExplorerには元のパスを渡す。
		Resolve(path);
		const auto absolute = PathUtils::ResolveFromExecutable(path);
		const auto check = [&](HRESULT result) {
			if (FAILED(result)) {
				throw std::runtime_error(std::format(
				    "Could not show {} in Explorer (0x{:08X}).",
				    PathUtils::ToUtf8(absolute),
				    static_cast<unsigned long>(result)));
			}
		};

		PIDLIST_ABSOLUTE parsed = nullptr;
		const auto result =
		    SHParseDisplayName(absolute.c_str(), nullptr, &parsed, 0, nullptr);
		const std::unique_ptr<std::remove_pointer_t<PIDLIST_ABSOLUTE>,
		                      decltype(&CoTaskMemFree)>
		    item(parsed, CoTaskMemFree);
		check(result);
		check(SHOpenFolderAndSelectItems(item.get(), 0, nullptr, 0));
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool ShellUtils::RevealLocationInExplorer(const std::filesystem::path &path,
                                           std::string &error) {
	try {
		if (path.empty()) {
			throw std::invalid_argument("Choose a valid local path.");
		}
		const auto absolute = PathUtils::ResolveFromExecutable(path);
		std::error_code status;
		if (std::filesystem::exists(absolute, status)) {
			return RevealInExplorer(absolute, error);
		}
		if (status && status != std::errc::no_such_file_or_directory &&
		    status != std::errc::not_a_directory) {
			throw std::system_error(status, "Could not check the file location");
		}
		for (auto parent = absolute.parent_path(); !parent.empty();) {
			if (std::filesystem::is_directory(parent, status)) {
				return OpenDirectory(parent, error);
			}
			const auto next = parent.parent_path();
			if (next == parent) {
				break;
			}
			parent = next;
		}
		error = "No existing parent folder was found.";
	} catch (const std::exception &failure) {
		error = failure.what();
	}
	return false;
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
		if (const auto process = OpenExecutable(path, visibility))
			CloseHandle(process);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

ShellUtils::ExecutableProcess::~ExecutableProcess() {
	Reset();
}

bool ShellUtils::ExecutableProcess::Start(const std::filesystem::path &path,
                                         std::string &error,
                                         WindowVisibility visibility,
                                         const std::wstring &arguments) {
	try {
		if (IsTracking())
			throw std::runtime_error("A game process is already being tracked.");
		handle_ = OpenExecutable(path, visibility, arguments);
		if (!IsTracking())
			throw std::runtime_error("Windows did not return a process handle.");
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool ShellUtils::ExecutableProcess::Poll(
    std::optional<std::uint32_t> &exitCode, std::string &error) const {
	exitCode.reset();
	if (!IsTracking()) {
		error = "No process is being tracked.";
		return false;
	}
	const auto state = WaitForSingleObject(handle_, 0);
	if (state == WAIT_TIMEOUT) {
		error.clear();
		return true;
	}
	DWORD code = 0;
	if (state != WAIT_OBJECT_0 || !GetExitCodeProcess(handle_, &code)) {
		error = std::format("Could not check the game process (Windows error {}).",
		                     GetLastError());
		return false;
	}
	exitCode = code;
	error.clear();
	return true;
}

void *ShellUtils::ExecutableProcess::FindRunningWindow(std::string &error) const {
	std::optional<std::uint32_t> exitCode;
	if (!Poll(exitCode, error)) {
		return nullptr;
	}
	if (exitCode) {
		error = "The game process has already exited.";
		return nullptr;
	}

	ProcessWindow target{GetProcessId(handle_)};
	if (target.processId == 0) {
		error = "Could not identify the running game process.";
		return nullptr;
	}
	EnumWindows(FindProcessWindow, reinterpret_cast<LPARAM>(&target));
	if (!target.window) {
		error = "The running game has no visible window yet.";
	}
	return target.window;
}

bool ShellUtils::ExecutableProcess::BringToForeground(std::string &error) const {
	const auto window = static_cast<HWND>(FindRunningWindow(error));
	if (!window) {
		return false;
	}

	if (IsIconic(window)) {
		ShowWindowAsync(window, SW_RESTORE);
	}
	if (SetForegroundWindow(window)) {
		error.clear();
		return true;
	}

	FLASHWINFO flash{sizeof(FLASHWINFO), window, FLASHW_TRAY, 3, 0};
	FlashWindowEx(&flash);
	error = "Windows kept the current window in front. Switch to the game from the taskbar.";
	return false;
}

bool ShellUtils::ExecutableProcess::RequestClose(std::string &error) const {
	const auto window = static_cast<HWND>(FindRunningWindow(error));
	if (!window) {
		return false;
	}
	if (!PostMessageW(window, WM_CLOSE, 0, 0)) {
		error = std::format("Could not request game exit (Windows error {}).",
		                    GetLastError());
		return false;
	}
	error.clear();
	return true;
}

void ShellUtils::ExecutableProcess::Reset() {
	if (handle_)
		CloseHandle(handle_);
	handle_ = nullptr;
}
