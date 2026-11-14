#include "files/OutputDirectory.h"
#include "platform/windows/PathUtils.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;

/// <summary>
/// 作成したファイルを識別し、別のファイルに置き換わっていないか確認する
/// </summary>
struct FileIdentity {
	DWORD volume = 0;
	DWORD high = 0;
	DWORD low = 0;

	bool Matches(const BY_HANDLE_FILE_INFORMATION &info) const {
		return volume == info.dwVolumeSerialNumber &&
		       high == info.nFileIndexHigh && low == info.nFileIndexLow;
	}
};

/// <summary>
/// ファイル操作に使用するWindowsハンドルを保持する
/// </summary>
class FileHandle {
  public:
	explicit FileHandle(HANDLE value) : value_(value) {
	}

	~FileHandle() {
		if (value_ != INVALID_HANDLE_VALUE) {
			CloseHandle(value_);
		}
	}

	FileHandle(const FileHandle &) = delete;
	FileHandle &operator=(const FileHandle &) = delete;

	HANDLE Get() const {
		return value_;
	}

	HANDLE Release() {
		return std::exchange(value_, INVALID_HANDLE_VALUE);
	}

  private:
	HANDLE value_;
};

/// <summary>
/// 操作対象とWindowsの失敗理由を通知する
/// </summary>
/// <param name="operation">失敗した操作</param>
/// <param name="path">対象の絶対パス</param>
/// <param name="code">失敗直後に取得したWindowsエラー番号</param>
/// <param name="readingSource">コピー元を読んでいるときはtrue</param>
[[noreturn]] void FileError(const char *operation, const fs::path &path,
                            DWORD code, bool readingSource = false) {
	throw OutputFileError(std::string(operation) + ": " +
	                          PathUtils::ToUtf8(path) + " (Windows error " +
	                          std::to_string(code) + ").",
	                      path, readingSource);
}

} // namespace

/// <summary>
/// 新規出力へ直接書き込み、失敗時は今回作成したものだけを片付ける
/// </summary>
struct OutputDirectory::Impl {
  public:
	void Create(const fs::path &path) {
		root_ = path;
		EnsureDirectory(path.parent_path());
		CreateOwnedDirectory(path, true);
	}

	void OpenExisting(const fs::path &path) {
		root_ = path;
	}

	void CreateSubdirectory(const fs::path &relative) {
		const auto path = OutputPath(relative);
		EnsureDirectory(path.parent_path());
		CreateOwnedDirectory(path, true);
	}

	void Copy(const fs::path &source, const fs::path &relative) {
		FileHandle input(CreateFileW(
		    source.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
		    nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));

		if (input.Get() == INVALID_HANDLE_VALUE) {
			FileError("Could not read resource", source, GetLastError(), true);
		}

		BY_HANDLE_FILE_INFORMATION info{};

		if (!GetFileInformationByHandle(input.Get(), &info)) {
			FileError("Could not inspect resource", source, GetLastError(), true);
		}

		if (info.dwFileAttributes &
		    (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) {
			throw OutputFileError(
			    "A resource is no longer a regular file: " +
			    PathUtils::ToUtf8(source), source, true);
		}

		FileHandle output(OpenNewFile(relative));
		std::array<char, 65536> buffer{};

		for (;;) {
			DWORD count = 0;

			if (!ReadFile(input.Get(), buffer.data(), DWORD(buffer.size()),
			              &count, nullptr)) {
				FileError("Could not read resource", source, GetLastError(), true);
			}

			if (count == 0) {
				break;
			}

			Write(output.Get(), buffer.data(), count, root_ / relative);
		}
	}

	void Text(const fs::path &relative, const std::string &text) {
		FileHandle output(OpenNewFile(relative));
		Write(output.Get(), text.data(), text.size(), root_ / relative);
	}

	void Complete() {
		entries_.clear();
	}

	std::string Cleanup() {
		std::size_t remaining = 0;
		std::string first;

		for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
			try {
				FileHandle handle(CreateFileW(
				    it->path.c_str(), DELETE | FILE_READ_ATTRIBUTES,
				    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				    nullptr, OPEN_EXISTING,
				    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
				    nullptr));

				if (handle.Get() == INVALID_HANDLE_VALUE) {
					const DWORD code = GetLastError();

					if (code == ERROR_FILE_NOT_FOUND ||
					    code == ERROR_PATH_NOT_FOUND) {
						continue;
					}

					FileError("Could not remove incomplete output", it->path,
					          code);
				}

				BY_HANDLE_FILE_INFORMATION info{};

				if (!GetFileInformationByHandle(handle.Get(), &info)) {
					FileError("Could not inspect incomplete output", it->path,
					          GetLastError());
				}

				if (!it->identified || !it->identity.Matches(info)) {
					throw std::runtime_error(
					    "An output item changed and was left untouched: " +
					    PathUtils::ToUtf8(it->path));
				}

				FILE_DISPOSITION_INFO disposition{TRUE};

				if (!SetFileInformationByHandle(
				        handle.Get(), FileDispositionInfo, &disposition,
				        sizeof(disposition))) {
					FileError("Could not remove incomplete output", it->path,
					          GetLastError());
				}
			} catch (const std::exception &e) {
				++remaining;

				if (first.empty()) {
					first = e.what();
				}
			}
		}

		entries_.clear();

		if (remaining == 0) {
			return {};
		}

		return " Cleanup left " + std::to_string(remaining) +
		       " item(s). Output: " + PathUtils::ToUtf8(root_) + ". " + first;
	}

  private:
	struct Entry {
		fs::path path;
		FileIdentity identity{};
		bool identified = false;
	};

	fs::path root_;
	std::vector<Entry> entries_;

	void Identify(HANDLE handle, Entry &entry) {
		BY_HANDLE_FILE_INFORMATION info{};

		if (!GetFileInformationByHandle(handle, &info)) {
			FileError("Could not identify new output", entry.path,
			          GetLastError());
		}

		entry.identity = {info.dwVolumeSerialNumber, info.nFileIndexHigh,
		                  info.nFileIndexLow};
		entry.identified = true;
	}

	void CreateOwnedDirectory(const fs::path &path, bool exclusive) {
		entries_.push_back({path});

		if (!CreateDirectoryW(path.c_str(), nullptr)) {
			const DWORD code = GetLastError();
			entries_.pop_back();

			if (!exclusive && code == ERROR_ALREADY_EXISTS &&
			    fs::is_directory(path)) {
				return;
			}

			FileError("Could not create a new output folder; existing folders "
			          "are not overwritten",
			          path, code);
		}

		FileHandle handle(CreateFileW(
		    path.c_str(), FILE_READ_ATTRIBUTES,
		    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		    OPEN_EXISTING,
		    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
		    nullptr));

		if (handle.Get() == INVALID_HANDLE_VALUE) {
			FileError("Could not inspect new output folder", path,
			          GetLastError());
		}

		Identify(handle.Get(), entries_.back());
	}

	void EnsureDirectory(const fs::path &path) {
		if (path.empty() || fs::is_directory(path)) {
			return;
		}

		EnsureDirectory(path.parent_path());
		CreateOwnedDirectory(path, false);
	}

	fs::path OutputPath(const fs::path &relative) const {
		const auto normalized = relative.lexically_normal();

		if (normalized.empty() || normalized.has_root_path() ||
		    !normalized.has_filename()) {
			throw std::invalid_argument("Use a relative output file path.");
		}

		for (const auto &part : normalized) {
			if (!PathUtils::IsValidFileName(part)) {
				throw std::invalid_argument(
				    "Output paths must stay inside the new folder.");
			}
		}

		return root_ / normalized;
	}

	HANDLE OpenNewFile(const fs::path &relative) {
		const auto path = OutputPath(relative);
		EnsureDirectory(path.parent_path());
		entries_.push_back({path});
		FileHandle handle(CreateFileW(
		    path.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
		    nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));

		if (handle.Get() == INVALID_HANDLE_VALUE) {
			const DWORD code = GetLastError();
			entries_.pop_back();
			FileError("Could not create a new output file", path, code);
		}

		Identify(handle.Get(), entries_.back());
		// 書き込み中はこのハンドルだけを所有し、呼び出し側で必ず閉じる。
		return handle.Release();
	}

	static void Write(HANDLE handle, const char *data, std::size_t size,
	                  const fs::path &path) {
		while (size != 0) {
			const DWORD chunk = DWORD((std::min)(size, std::size_t(65536)));
			DWORD written = 0;

			if (!WriteFile(handle, data, chunk, &written, nullptr)) {
				FileError("Could not write output", path, GetLastError());
			}

			if (written == 0) {
				FileError("Could not write output", path, ERROR_WRITE_FAULT);
			}

			data += written;
			size -= written;
		}
	}
};

OutputDirectory::OutputDirectory() noexcept = default;
OutputDirectory::~OutputDirectory() = default;

void OutputDirectory::Create(const std::filesystem::path &path) {
	if (impl_) {
		throw std::logic_error("Output creation has already started.");
	}

	if (!path.is_absolute() ||
	    path.native().find(L'\0') != std::wstring::npos) {
		throw std::invalid_argument("Choose an absolute output folder.");
	}

	impl_ = std::make_unique<Impl>();
	impl_->Create(path);
}

void OutputDirectory::OpenExisting(const std::filesystem::path &path) {
	if (impl_) {
		throw std::logic_error("Output creation has already started.");
	}

	if (!path.is_absolute() || path.native().find(L'\0') != std::wstring::npos ||
	    !std::filesystem::is_directory(std::filesystem::symlink_status(path))) {
		throw std::invalid_argument("Choose an existing absolute output folder.");
	}

	impl_ = std::make_unique<Impl>();
	impl_->OpenExisting(path);
}

void OutputDirectory::CreateSubdirectory(
    const std::filesystem::path &relative) {
	if (!impl_) {
		throw std::logic_error("Choose the output folder before creating a subfolder.");
	}

	impl_->CreateSubdirectory(relative);
}

void OutputDirectory::Copy(const std::filesystem::path &source,
                           const std::filesystem::path &relative) {
	if (!impl_) {
		throw std::logic_error("Create the output folder before copying.");
	}

	if (!source.is_absolute() ||
	    source.native().find(L'\0') != std::wstring::npos) {
		throw std::invalid_argument(
		    "Choose a regular source file with an absolute path.");
	}

	impl_->Copy(source, relative);
}

void OutputDirectory::Text(const std::filesystem::path &relative,
                           const std::string &text) {
	if (!impl_) {
		throw std::logic_error("Create the output folder before writing.");
	}

	impl_->Text(relative, text);
}

void OutputDirectory::Complete() {
	if (impl_) {
		impl_->Complete();
	}
}

std::string OutputDirectory::Cleanup() {
	return impl_ ? impl_->Cleanup() : std::string{};
}
