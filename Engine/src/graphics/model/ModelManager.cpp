#include "graphics/model/ModelManager.h"
#include "core/Logger.h"

#include "graphics/model/Model.h"
#include "graphics/model/ModelData.h"
#include "graphics/model/ModelLoader.h"
#include "graphics/render/D3D12Graphics.h"
#include "platform/windows/PathUtils.h"

#include <Windows.h>
#include <algorithm>
#include <exception>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace {

/// <summary>
/// モデルの最後の共有参照がなくなったときにリソースを解放する
/// </summary>
/// <param name="model">解放するモデル</param>
void DestroyModel(Model *model) {
	if (!model) {
		return;
	}

	model->Finalize();
	delete model;
}

struct FileStamp {
	std::filesystem::file_time_type modified;
	std::uintmax_t size;
	bool operator==(const FileStamp &) const = default;
};

/// <summary>
/// 参照ファイルの更新情報を取得する。未作成の材質ファイルも後から検出できるようにする
/// </summary>
std::optional<FileStamp> ReadFileStamp(const std::filesystem::path &path) {
	std::error_code error;
	const auto modified = std::filesystem::last_write_time(path, error);
	if (error) {
		return std::nullopt;
	}
	const auto size = std::filesystem::file_size(path, error);
	if (error) {
		return std::nullopt;
	}
	return FileStamp{modified, size};
}

} // namespace

/// <summary>
/// モデルのキャッシュと描画基盤への参照を保持する
/// </summary>
struct ModelManager::Impl {
	/// <summary>
	/// Windows上の同じ正規化パスを、大文字・小文字を区別せず共有する
	/// </summary>
	struct PathLess {
		bool operator()(const std::filesystem::path &left,
		                const std::filesystem::path &right) const {
			return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1,
			                            TRUE) == CSTR_LESS_THAN;
		}
	};

	explicit Impl(D3D12Graphics &graphics) : graphics(graphics) {
	}

	// 参照先はModelManagerより長く保持する
	D3D12Graphics &graphics;

	// 正規化した絶対パスと、読み込み済みモデルの対応
	struct Entry {
		std::shared_ptr<const Model> model;
		std::filesystem::file_time_type modified;
		std::uintmax_t size;
		std::vector<std::pair<std::filesystem::path, std::optional<FileStamp>>> dependencies;
	};
	std::map<std::filesystem::path, Entry, PathLess> cache;

	bool finalized = false;
	std::uint64_t reloadRevision = 0;
	std::uint64_t cacheRevision = 0;
};

ModelManager::ModelManager(D3D12Graphics &graphics)
    : impl_(std::make_unique<Impl>(graphics)) {
}

ModelManager::~ModelManager() {
	Finalize();
}

ModelHandle ModelManager::Load(const std::filesystem::path &filePath) {
	return LoadFile(filePath, LoadMode::CACHED);
}

ModelHandle ModelManager::LoadUpdated(const std::filesystem::path &filePath) {
	return LoadFile(filePath, LoadMode::CHECK_FOR_CHANGES);
}

ModelHandle
ModelManager::FindLoaded(const std::filesystem::path &filePath) const {
	if (impl_->finalized || filePath.empty() ||
	    (filePath.has_root_path() && !filePath.is_absolute()) ||
	    filePath.native().find(L'\0') != std::wstring::npos) {
		return {};
	}

	try {
		const auto key = std::filesystem::weakly_canonical(
		    PathUtils::ResolveFromExecutable(filePath));
		const auto found = impl_->cache.find(key);

		if (found == impl_->cache.end()) {
			return {};
		}

		ModelHandle handle;
		handle.model_ = found->second.model;
		handle.graphics_ = &impl_->graphics;
		return handle;
	} catch (const std::filesystem::filesystem_error &) {
		return {};
	}
}

ModelHandle ModelManager::Reload(const std::filesystem::path &filePath) {
	return LoadFile(filePath, LoadMode::FORCE_RELOAD);
}

std::uint64_t ModelManager::GetReloadRevision() const {
	return impl_->reloadRevision;
}

std::uint64_t ModelManager::GetCacheRevision() const {
	return impl_->cacheRevision;
}

ModelHandle ModelManager::LoadFile(const std::filesystem::path &filePath,
                                   LoadMode mode) {
	if (impl_->finalized) {
		Logger::Error("ModelManager::Load: 終了済みです。");
		return {};
	}

	if (filePath.empty() ||
	    filePath.native().find(L'\0') != std::wstring::npos) {
		Logger::Error("ModelManager::Load: ファイルパスが無効です。");
		return {};
	}

	auto *device = impl_->graphics.GetDevice();
	auto *commandQueue = impl_->graphics.GetCommandQueue();

	if (!device || !commandQueue) {
		Logger::Error("ModelManager::Load: 描画基盤が未初期化です。");
		return {};
	}

	try {
		// 相対パスは実行ファイルのフォルダを基準に解決する
		const auto resolvedPath = PathUtils::ResolveFromExecutable(filePath);

		// ファイル削除時も、同じ正規化パスの有効なキャッシュを取得できるようにする
		const std::filesystem::path key =
		    std::filesystem::weakly_canonical(resolvedPath);

		const auto found = impl_->cache.find(key);
		std::filesystem::file_time_type modified{};
		std::uintmax_t size = 0;
		if (mode != LoadMode::CACHED || found == impl_->cache.end()) {
			modified = std::filesystem::last_write_time(key);
			size = std::filesystem::file_size(key);
		}
		const bool reload = mode == LoadMode::FORCE_RELOAD ||
		    (mode == LoadMode::CHECK_FOR_CHANGES && found != impl_->cache.end() &&
		     (found->second.modified != modified || found->second.size != size ||
		      std::ranges::any_of(found->second.dependencies, [](const auto &dependency) {
			      return ReadFileStamp(dependency.first) != dependency.second;
		      })));

		if (!reload && found != impl_->cache.end()) {
			ModelHandle handle;
			handle.model_ = found->second.model;
			handle.graphics_ = &impl_->graphics;
			return handle;
		}

		if (reload && impl_->reloadRevision ==
		                  (std::numeric_limits<std::uint64_t>::max)()) {
			throw std::runtime_error("Model reload revision limit reached.");
		}

		if (impl_->cacheRevision ==
		    (std::numeric_limits<std::uint64_t>::max)()) {
			throw std::runtime_error("Model cache revision limit reached.");
		}

		// ファイルからCPU側のモデルデータを読み込む
		ModelData data;

		std::vector<std::filesystem::path> dependencies;
		if (!ModelLoader::Load(key, data, &dependencies)) {
			return {};
		}
		Impl::Entry entry{nullptr, modified, size, {}};
		entry.dependencies.reserve(dependencies.size());
		for (auto &path : dependencies) {
			const auto stamp = ReadFileStamp(path);
			entry.dependencies.emplace_back(std::move(path), stamp);
		}

		// 最後の共有参照がなくなったときにFinalizeを呼び出す
		std::shared_ptr<Model> model(new Model{}, DestroyModel);

		// GPU側の共有リソースを作成する
		if (!model->Initialize(device, commandQueue, data)) {
			return {};
		}

		// 初期化に成功したモデルだけをキャッシュへ登録する
		entry.model = model;
		impl_->cache.insert_or_assign(key, std::move(entry));
		++impl_->cacheRevision;

		if (reload) {
			++impl_->reloadRevision;
		}

		ModelHandle handle;
		handle.model_ = model;
		handle.graphics_ = &impl_->graphics;
		return handle;
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "ModelManagerの読み込み中に例外が発生しました: {}", error.what()));

		return {};
	}
}

void ModelManager::Finalize() {
	if (impl_->finalized) {
		return;
	}

	// キャッシュからの共有参照を解放する。
	// ハンドルから参照されているモデルは、その参照がなくなるまで保持する。
	impl_->cache.clear();

	// 終了後の再読み込みは禁止する
	impl_->finalized = true;
}

bool ModelManager::GetLocalBounds(
    const ModelHandle &model, std::array<DirectX::XMFLOAT3, 8> &corners) const {
	return model.model_ && model.graphics_ == &impl_->graphics &&
	       model.model_->GetBoundsCorners(corners);
}

std::shared_ptr<const ModelAnimationData>
ModelManager::GetAnimationData(const ModelHandle &model) const {
	return model.graphics_ == &impl_->graphics && model.model_
	           ? model.model_->GetAnimationData()
	           : nullptr;
}

std::size_t ModelManager::GetMaterialCount(const ModelHandle &model) const {
	return model.model_ && model.graphics_ == &impl_->graphics
	           ? model.model_->GetMaterialCount()
	           : 0;
}

std::string_view
ModelManager::GetMaterialName(const ModelHandle &model,
                              std::size_t materialIndex) const {
	return model.model_ && model.graphics_ == &impl_->graphics
	           ? model.model_->GetMaterialName(materialIndex)
	           : std::string_view{};
}

std::size_t
ModelManager::GetMaterialMeshCount(const ModelHandle &model,
                                   std::size_t materialIndex) const {
	return model.model_ && model.graphics_ == &impl_->graphics
	           ? model.model_->GetMaterialMeshCount(materialIndex)
	           : 0;
}
