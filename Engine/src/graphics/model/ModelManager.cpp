#include "graphics/model/ModelManager.h"
#include "core/Logger.h"

#include "graphics/model/Model.h"
#include "graphics/model/ModelData.h"
#include "graphics/model/ModelLoader.h"
#include "graphics/render/D3D12Graphics.h"
#include "platform/windows/PathUtils.h"

#include <Windows.h>
#include <exception>
#include <format>
#include <limits>
#include <map>

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
	std::map<std::filesystem::path, std::shared_ptr<const Model>, PathLess>
	    cache;

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
	return LoadFile(filePath, false);
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
		handle.model_ = found->second;
		handle.graphics_ = &impl_->graphics;
		return handle;
	} catch (const std::filesystem::filesystem_error &) {
		return {};
	}
}

ModelHandle ModelManager::Reload(const std::filesystem::path &filePath) {
	return LoadFile(filePath, true);
}

std::uint64_t ModelManager::GetReloadRevision() const {
	return impl_->reloadRevision;
}

std::uint64_t ModelManager::GetCacheRevision() const {
	return impl_->cacheRevision;
}

ModelHandle ModelManager::LoadFile(const std::filesystem::path &filePath,
                                   bool reload) {
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

		// 読み込み済みなら、同じモデルへの共有参照を返す
		const auto found = impl_->cache.find(key);

		if (!reload && found != impl_->cache.end()) {
			ModelHandle handle;
			handle.model_ = found->second;
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

		if (!ModelLoader::Load(key, data)) {
			return {};
		}

		// 最後の共有参照がなくなったときにFinalizeを呼び出す
		std::shared_ptr<Model> model(new Model{}, DestroyModel);

		// GPU側の共有リソースを作成する
		if (!model->Initialize(device, commandQueue, data)) {
			return {};
		}

		// 初期化に成功したモデルだけをキャッシュへ登録する
		impl_->cache.insert_or_assign(key, model);
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
