#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"

#include <Windows.h>
#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
/// <summary>
/// 削除共有された読み手の旧内容を保って置換する。拡張APIが未対応の場合だけ従来方式を使う
/// </summary>
/// <param name="handle">書き込みと削除の権限で所有する保存ファイル</param>
/// <param name="info">保存先の名前と、事前に確保した置換情報</param>
/// <param name="size">置換情報全体のバイト数</param>
/// <param name="overwrite">既存の保存先を置き換えるか</param>
/// <param name="error">失敗した操作のWindowsエラー。成功時はERROR_SUCCESS</param>
/// <returns>true:名前変更成功 false:名前変更失敗。アクセス拒否や共有違反は再試行しない</returns>
bool RenameSavedFile(HANDLE handle, FILE_RENAME_INFO &info, DWORD size,
                     bool overwrite, DWORD &error) {
	info.Flags = overwrite ? FILE_RENAME_FLAG_REPLACE_IF_EXISTS |
	                             FILE_RENAME_FLAG_POSIX_SEMANTICS
	                       : 0;

	if (SetFileInformationByHandle(handle, FileRenameInfoEx, &info, size)) {
		error = ERROR_SUCCESS;
		return true;
	}

	error = GetLastError();

	if (error != ERROR_INVALID_PARAMETER && error != ERROR_NOT_SUPPORTED &&
	    error != ERROR_INVALID_FUNCTION &&
	    error != ERROR_CALL_NOT_IMPLEMENTED) {
		return false;
	}

	// 古いOSやファイルシステムでは、置換の可否を従来の共有規則に従わせる。
	info.Flags = 0;
	info.ReplaceIfExists = overwrite ? TRUE : FALSE;

	if (SetFileInformationByHandle(handle, FileRenameInfo, &info, size)) {
		error = ERROR_SUCCESS;
		return true;
	}

	error = GetLastError();
	return false;
}

/// <summary>
/// 作成した一時ファイルのハンドルを保持し、未確定なら同じファイルだけを削除する
/// </summary>
class StagedFile {
  public:
	StagedFile() = default;
	StagedFile(const StagedFile &) = delete;
	StagedFile &operator=(const StagedFile &) = delete;

	~StagedFile() {
		Discard();
	}

	/// <summary>
	/// 他の保存と競合しない新規ファイルを、書き込み・削除用に排他的に開く
	/// </summary>
	/// <param name="path">作成する一時ファイル</param>
	/// <returns>true:作成成功 false:既存ファイルあり、または作成失敗</returns>
	bool Create(const std::filesystem::path &path) {
		handle_ = CreateFileW(path.c_str(), GENERIC_WRITE | DELETE, 0, nullptr,
		                      CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		return handle_ != INVALID_HANDLE_VALUE;
	}

	/// <summary>
	/// 書き込みと名前変更に使用するハンドルを取得する
	/// </summary>
	/// <returns>所有しているハンドル</returns>
	HANDLE Get() const {
		return handle_;
	}

	/// <summary>
	/// 置換成功後のファイルを、削除せずに閉じる対象へ切り替える
	/// </summary>
	void Commit() noexcept {
		staged_ = false;
	}

	/// <summary>
	/// 未確定ファイルをハンドルで削除対象にし、閉じる。確定済みなら閉じるだけとする
	/// </summary>
	/// <returns>true:後片付け成功または未作成 false:削除の予約またはクローズ失敗</returns>
	bool Discard() noexcept {
		if (handle_ == INVALID_HANDLE_VALUE) {
			return true;
		}

		FILE_DISPOSITION_INFO disposition{TRUE};
		const bool deleted = !staged_ || SetFileInformationByHandle(
		                                     handle_, FileDispositionInfo,
		                                     &disposition, sizeof(disposition));
		const bool closed = CloseHandle(handle_) != FALSE;
		handle_ = INVALID_HANDLE_VALUE;
		return deleted && closed;
	}

  private:
	HANDLE handle_ = INVALID_HANDLE_VALUE;
	bool staged_ = true;
};
} // namespace

bool FileUtils::WriteBytesAtomically(const std::filesystem::path &path,
                                     std::span<const std::byte> bytes,
                                     std::string &error, bool overwrite) {
	StagedFile file;
	std::filesystem::path temporary;

	try {
		if (path.empty()) {
			throw std::invalid_argument("Choose a save destination.");
		}

		const auto target = PathUtils::ResolveFromExecutable(path);

		if (!target.has_filename() ||
		    target.native().find(L'\0') != std::wstring::npos) {
			throw std::invalid_argument("Choose a valid save destination.");
		}

		temporary = target;
		temporary += L".tmp";
		const auto nameBytes = target.native().size() * sizeof(wchar_t);
		const auto renameSize =
		    offsetof(FILE_RENAME_INFO, FileName) + nameBytes + sizeof(wchar_t);

		if (renameSize > (std::numeric_limits<DWORD>::max)()) {
			throw std::length_error("The save destination is too long.");
		}

		// 作成後は、置換情報のためのメモリ確保やパスによる開き直しを行わない。
		std::vector<std::byte> renameBuffer(renameSize);
		auto *rename =
		    reinterpret_cast<FILE_RENAME_INFO *>(renameBuffer.data());
		rename->RootDirectory = nullptr;
		rename->FileNameLength = static_cast<DWORD>(nameBytes);
		std::copy(target.native().begin(), target.native().end(),
		          rename->FileName);

		if (!file.Create(temporary)) {
			throw std::runtime_error(
			    "Could not begin saving; another save or file may conflict "
			    "with the destination (Windows error " +
			    std::to_string(GetLastError()) + ").");
		}

		std::size_t offset = 0;

		while (offset < bytes.size()) {
			const auto size = static_cast<DWORD>(
			    (std::min)(bytes.size() - offset, std::size_t(65536)));
			DWORD written = 0;

			if (!WriteFile(file.Get(), bytes.data() + offset, size, &written,
			               nullptr) ||
			    written == 0) {
				throw std::runtime_error("Could not write the save file.");
			}

			offset += written;
		}

		if (!FlushFileBuffers(file.Get())) {
			throw std::runtime_error("Could not flush the save file.");
		}

		DWORD renameError = ERROR_SUCCESS;

		if (!RenameSavedFile(file.Get(), *rename,
		                     static_cast<DWORD>(renameSize), overwrite,
		                     renameError)) {
			throw std::runtime_error("Could not replace the destination; it "
			                         "may already exist or be in use "
			                         "(Windows error " +
			                         std::to_string(renameError) + ").");
		}

		file.Commit();
		file.Discard();
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		const bool cleaned = file.Discard();
		error = exception.what();

		if (!cleaned) {
			error += " The temporary file could not be removed: " +
			         PathUtils::ToUtf8(temporary);
		}

		return false;
	}
}

bool FileUtils::WriteTextAtomically(const std::filesystem::path &path,
                                    std::string_view text, std::string &error,
                                    bool overwrite) {
	return WriteBytesAtomically(
	    path, std::as_bytes(std::span{text.data(), text.size()}), error,
	    overwrite);
}
