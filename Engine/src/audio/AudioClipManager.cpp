#include "audio/AudioClipManager.h"

#include "audio/WaveReader.h"
#include "platform/windows/PathUtils.h"
#include <Windows.h>
#include <exception>
#include <map>
#include <stdexcept>
#include <utility>

/// <summary>
/// 同じ音声ファイルを共有するキャッシュ
/// </summary>
struct AudioClipManager::Impl {
	/// <summary>
	/// Windowsのパスを大文字・小文字を区別せず比較する
	/// </summary>
	struct PathLess {
		bool operator()(const std::filesystem::path &a,
		                const std::filesystem::path &b) const {
			return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) ==
			       CSTR_LESS_THAN;
		}
	};

	std::map<std::filesystem::path, AudioClipHandle, PathLess> cache;
};

AudioClipManager::AudioClipManager() : impl_(std::make_unique<Impl>()) {
}

AudioClipManager::~AudioClipManager() = default;

AudioClipHandle AudioClipManager::Create(WaveData data, std::string &error) {
	error.clear();
	try {
		if (!data.IsValid()) {
			throw std::invalid_argument(
			    "Audio samples or their format are invalid.");
		}

		AudioClipHandle handle;
		handle.data_ = std::make_shared<const WaveData>(std::move(data));
		return handle;
	} catch (const std::exception &exception) {
		error = exception.what();
		return {};
	}
}

AudioClipHandle AudioClipManager::Load(const std::filesystem::path &path,
                                       std::string &error) {
	return LoadFile(path, false, error);
}

AudioClipHandle AudioClipManager::Reload(const std::filesystem::path &path,
                                         std::string &error) {
	return LoadFile(path, true, error);
}

AudioClipHandle AudioClipManager::LoadFile(const std::filesystem::path &path,
                                           bool reload, std::string &error) {
	error.clear();
	try {
		if (path.empty() || !PathUtils::HasExtension(path, ".wav")) {
			throw std::invalid_argument("Choose a WAV audio file.");
		}

		const auto key = std::filesystem::weakly_canonical(
		    PathUtils::ResolveFromExecutable(path));
		const auto found = impl_->cache.find(key);
		if (!reload && found != impl_->cache.end()) {
			return found->second;
		}

		WaveData data;
		if (!WaveReader::Load(key, data, error)) {
			return {};
		}

		auto handle = Create(std::move(data), error);
		if (handle) {
			impl_->cache.insert_or_assign(key, handle);
		}

		return handle;
	} catch (const std::exception &exception) {
		error = exception.what();
		return {};
	}
}

void AudioClipManager::Clear() {
	impl_->cache.clear();
}
