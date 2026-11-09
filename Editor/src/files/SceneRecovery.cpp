#include "files/SceneRecovery.h"
#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"
#include "scene/SceneSerializer.h"
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <json.hpp>
#include <objbase.h>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace {
using Json = nlohmann::json;

struct Lock {
	HANDLE value = INVALID_HANDLE_VALUE;
	Lock() = default;
	Lock(const Lock &) = delete;
	Lock &operator=(const Lock &) = delete;

	~Lock() {
		if (value != INVALID_HANDLE_VALUE) {
			CloseHandle(value);
		}
	}
};

/// <summary>
/// GUIDを含む、この機能の復旧ファイル名か確認する
/// </summary>
bool RecoveryName(const std::filesystem::path &path) {
	const auto name = PathUtils::ToUtf8(path.filename());
	constexpr std::string_view prefix = "session-";
	constexpr std::string_view suffix = ".recovery.json";

	if (name.size() != prefix.size() + 36 + suffix.size() ||
	    !name.starts_with(prefix) || !name.ends_with(suffix)) {
		return false;
	}

	for (std::size_t i = 0; i < 36; ++i) {
		const char c = name[prefix.size() + i];

		if (i == 8 || i == 13 || i == 18 || i == 23) {
			if (c != '-') {
				return false;
			}
		} else if (!std::isxdigit(static_cast<unsigned char>(c))) {
			return false;
		}
	}

	return true;
}

/// <summary>
/// 候補の使用権を取得する。解放・プロセス終了時にはロック用ファイルも削除する
/// </summary>
void TakeLock(const std::filesystem::path &path, Lock &lock,
              bool create = false) {
	auto file = path;
	file += L".lock";
	lock.value =
	    CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0,
	                nullptr, create ? CREATE_NEW : OPEN_ALWAYS,
	                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
	if (lock.value == INVALID_HANDLE_VALUE) {
		throw std::system_error(
		    GetLastError(), std::system_category(),
		    "The recovery is in use, or its folder cannot be accessed.");
	}
}

/// <summary>
/// 復旧ファイルの形式と記録元のパスを読み込む
/// </summary>
Json ReadDocument(const std::filesystem::path &path) {
	const auto attributes = GetFileAttributesW(path.c_str());

	if (attributes == INVALID_FILE_ATTRIBUTES ||
	    (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
		throw std::runtime_error(
		    "The recovery file is missing or linked to another location.");
	}

	std::ifstream file(path, std::ios::binary);

	if (!file) {
		throw std::runtime_error("Could not open the recovery file.");
	}

	file.exceptions(std::ios::badbit);
	auto data = Json::parse(file);
	if (data.at("format") != "EditorRecovery" ||
	    !data.at("version").is_number_integer() || data.at("version") != 1 ||
	    !data.at("scene").is_object() || !data.at("session").is_string() ||
	    !data.at("capturedUtc").is_string()) {
		throw std::runtime_error("Unsupported recovery file.");
	}
	return data;
}

/// <summary>
/// 空、または通常の絶対パスとして元の保存先を読み込む
/// </summary>
std::filesystem::path SourceFile(const Json &data) {
	const auto path =
	    PathUtils::FromUtf8(data.at("sourceFile").get<std::string>());
	if (!path.empty() && !path.is_absolute()) {
		throw std::runtime_error("The original scene path is invalid.");
	}

	return path;
}

/// <summary>
/// UTCの記録時刻を、並べ替え可能な形式で取得する
/// </summary>
std::string Timestamp() {
	SYSTEMTIME time{};
	GetSystemTime(&time);
	return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} UTC",
	                   time.wYear, time.wMonth, time.wDay, time.wHour,
	                   time.wMinute, time.wSecond, time.wMilliseconds);
}
} // namespace

struct SceneRecovery::Impl {
	std::filesystem::path directory, path;
	std::string session;
	std::unique_ptr<Lock> lock;
	bool recorded = false;

	/// <summary>
	/// 同じフォルダ内の通常の候補パスだけを受け入れる
	/// </summary>
	std::filesystem::path Candidate(const std::filesystem::path &input) const {
		if (directory.empty() || !input.is_absolute()) {
			throw std::runtime_error("Choose a recovery in this folder.");
		}

		auto candidate = input.lexically_normal();
		auto relative = PathUtils::RelativeInside(directory, candidate);

		if (!relative.parent_path().empty() || !RecoveryName(relative)) {
			throw std::runtime_error("Choose a recovery in this folder.");
		}

		return candidate;
	}
};

SceneRecovery::SceneRecovery() : impl_(std::make_unique<Impl>()) {
}

SceneRecovery::~SceneRecovery() = default;

bool SceneRecovery::Initialize(const std::filesystem::path &directory,
                               std::string &error) {
	try {
		if (IsReady()) {
			throw std::runtime_error("The recovery session is already open.");
		}

		auto candidate = std::make_unique<Impl>();
		candidate->directory = PathUtils::ResolveFromExecutable(directory);

		if (candidate->directory.native().find(L'\0') != std::wstring::npos) {
			throw std::runtime_error("Invalid recovery folder.");
		}

		std::filesystem::create_directories(candidate->directory);
		candidate->directory = std::filesystem::canonical(candidate->directory);
		GUID id{};
		wchar_t text[39]{};

		if (FAILED(CoCreateGuid(&id)) || StringFromGUID2(id, text, 39) != 39) {
			throw std::runtime_error("Could not create a recovery session ID.");
		}

		candidate->session = PathUtils::ToUtf8(
		    std::filesystem::path(std::wstring(text + 1, 36)));
		candidate->path = candidate->directory /
		                  PathUtils::FromUtf8("session-" + candidate->session +
		                                      ".recovery.json");
		if (std::filesystem::exists(candidate->path)) {
			throw std::runtime_error("The recovery session already exists.");
		}

		candidate->lock = std::make_unique<Lock>();
		TakeLock(candidate->path, *candidate->lock, true);
		impl_ = std::move(candidate);
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool SceneRecovery::Record(const Scene &scene,
                           const std::filesystem::path &sourceFile,
                           const AssetSources &assets, const GameConfig &game,
                           std::string &error) {
	try {
		if (!IsReady()) {
			throw std::runtime_error("Recovery is not initialized.");
		}

		if (!sourceFile.empty() &&
		    (!sourceFile.is_absolute() ||
		     sourceFile.native().find(L'\0') != std::wstring::npos)) {
			throw std::runtime_error("Invalid original scene path.");
		}

		auto temporary = impl_->path;
		temporary += L".tmp";

		if (!sourceFile.empty() &&
		    (PathUtils::IsSameFile(sourceFile, impl_->path) ||
		     PathUtils::IsSameFile(sourceFile, temporary))) {
			throw std::runtime_error(
			    "The scene conflicts with recovery storage.");
		}

		if (std::filesystem::exists(impl_->path) &&
		    (!impl_->recorded ||
		     ReadDocument(impl_->path).at("session") != impl_->session)) {
			throw std::runtime_error(
			    "The recovery file was replaced outside the editor.");
		}

		std::string sceneText, gameText;

		if (!SceneSerializer::Serialize(scene, sceneText) ||
		    !game.Serialize(gameText, error)) {
			throw std::runtime_error(
			    "The editing scene or game settings could not be recorded.");
		}

		Json directories = Json::array();

		for (const auto &path : assets.GetDirectories()) {
			directories.push_back(PathUtils::ToUtf8(path));
		}

		Json data{{"format", "EditorRecovery"},
		          {"version", 1},
		          {"session", impl_->session},
		          {"sourceFile", PathUtils::ToUtf8(sourceFile)},
		          {"capturedUtc", Timestamp()},
		          {"scene", Json::parse(sceneText)},
		          {"game", Json::parse(gameText)},
		          {"assetDirectory", PathUtils::ToUtf8(assets.GetDirectory())},
		          {"assetDirectories", std::move(directories)}};
		if (!FileUtils::WriteTextAtomically(impl_->path, data.dump(2), error)) {
			return false;
		}

		impl_->recorded = true;
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool SceneRecovery::Enumerate(std::vector<Entry> &entries,
                              std::string &error) const {
	try {
		if (impl_->directory.empty()) {
			throw std::runtime_error("Recovery is not initialized.");
		}

		std::vector<Entry> result;

		for (const auto &file :
		     std::filesystem::directory_iterator(impl_->directory)) {
			if (!RecoveryName(file.path()) || file.is_directory()) {
				continue;
			}

			Lock lock;

			try {
				TakeLock(file.path(), lock);
			} catch (const std::system_error &lockError) {
				if (lockError.code().value() == ERROR_SHARING_VIOLATION ||
				    lockError.code().value() == ERROR_LOCK_VIOLATION) {
					continue;
				}

				Entry unavailable;
				unavailable.path = file.path();
				unavailable.error = lockError.what();
				result.push_back(std::move(unavailable));
				continue;
			}

			Entry entry;
			entry.path = file.path();

			try {
				auto data = ReadDocument(entry.path);
				entry.sourceFile = SourceFile(data);
				entry.capturedUtc = data.at("capturedUtc").get<std::string>();
			} catch (const std::exception &e) {
				entry.error = e.what();
			}

			result.push_back(std::move(entry));
		}

		std::sort(result.begin(), result.end(),
		          [](const Entry &a, const Entry &b) {
			          return a.capturedUtc != b.capturedUtc
			                     ? a.capturedUtc > b.capturedUtc
			                     : a.path < b.path;
		          });
		entries.swap(result);
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool SceneRecovery::Read(const std::filesystem::path &path, Snapshot &snapshot,
                         std::string &error) const {
	try {
		auto candidatePath = impl_->Candidate(path);
		Lock lock;
		TakeLock(candidatePath, lock);
		auto data = ReadDocument(candidatePath);
		Snapshot candidate;
		candidate.sourceFile = SourceFile(data);
		candidate.capturedUtc = data.at("capturedUtc").get<std::string>();
		if (!SceneSerializer::Deserialize(candidate.scene,
		                                  data.at("scene").dump()) ||
		    !candidate.game.Deserialize(data.at("game").dump(), error)) {
			throw std::runtime_error(
			    "The recovery scene or game settings are invalid.");
		}
		if (!data.at("assetDirectories").is_array()) {
			throw std::runtime_error("Invalid asset sources.");
		}

		for (const auto &directory : data.at("assetDirectories")) {
			candidate.assets.Register(
			    PathUtils::FromUtf8(directory.get<std::string>()));
		}

		candidate.assets.Select(
		    PathUtils::FromUtf8(data.at("assetDirectory").get<std::string>()));
		snapshot.scene.CopyFrom(candidate.scene);
		snapshot.assets = std::move(candidate.assets);
		snapshot.game = std::move(candidate.game);
		snapshot.sourceFile.swap(candidate.sourceFile);
		snapshot.capturedUtc.swap(candidate.capturedUtc);
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool SceneRecovery::Remove(const std::filesystem::path &path,
                           std::string &error) const {
	try {
		auto candidate = impl_->Candidate(path);

		if (std::filesystem::is_directory(candidate)) {
			throw std::runtime_error("Choose a recovery file, not a folder.");
		}

		Lock lock;
		TakeLock(candidate, lock);
		std::filesystem::remove(candidate);
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool SceneRecovery::Clear(const std::filesystem::path &protectedFile,
                          std::string &error) {
	try {
		if (!impl_->recorded) {
			error.clear();
			return true;
		}

		if (!IsReady()) {
			throw std::runtime_error("Recovery is not initialized.");
		}

		if (!protectedFile.empty() &&
		    PathUtils::IsSameFile(protectedFile, impl_->path)) {
			throw std::runtime_error(
			    "The active scene is using the recovery file.");
		}

		if (std::filesystem::exists(impl_->path)) {
			if (ReadDocument(impl_->path).at("session") != impl_->session) {
				throw std::runtime_error(
				    "The recovery file was replaced outside the editor.");
			}

			std::filesystem::remove(impl_->path);
		}

		impl_->recorded = false;
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

void SceneRecovery::Release() {
	impl_->lock.reset();
}

bool SceneRecovery::IsReady() const {
	return impl_->lock != nullptr;
}

const std::filesystem::path &SceneRecovery::GetCurrentPath() const {
	return impl_->path;
}
