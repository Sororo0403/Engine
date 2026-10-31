#include "graphics/model/ModelManager.h"
#include "core/Logger.h"

#include "graphics/model/Model.h"
#include "graphics/model/ModelData.h"
#include "graphics/model/ModelLoader.h"
#include "graphics/render/D3D12Graphics.h"
#include "platform/windows/PathUtils.h"

#include <exception>
#include <format>
#include <unordered_map>

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
	explicit Impl(D3D12Graphics &graphics) : graphics(graphics) {}

	// 参照先はModelManagerより長く保持する
	D3D12Graphics &graphics;

	// 正規化した絶対パスと、読み込み済みモデルの対応
	std::unordered_map<std::filesystem::path, std::shared_ptr<const Model>>
	    cache;

	bool finalized = false;
};

ModelManager::ModelManager(D3D12Graphics &graphics)
    : impl_(std::make_unique<Impl>(graphics)) {}

ModelManager::~ModelManager() { Finalize(); }

ModelHandle ModelManager::Load(const std::filesystem::path &filePath) {
	if (impl_->finalized) {
		Logger::Error("ModelManager::Load: 終了済みです。");
		return {};
	}

	if (filePath.empty()) {
		Logger::Error("ModelManager::Load: ファイルパスが空です。");
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
		std::filesystem::path resolvedPath = filePath;

		if (resolvedPath.is_relative()) {
			const auto executableDirectory =
			    PathUtils::GetExecutableDirectory();

			if (executableDirectory.empty()) {
				Logger::Error("ModelManager::Load: 実行ファイルのフォルダを"
				              "取得できませんでした。");
				return {};
			}

			resolvedPath = executableDirectory / resolvedPath;
		}

		// 実在するファイルの正規化済み絶対パスをキャッシュのキーにする
		const std::filesystem::path key =
		    std::filesystem::canonical(resolvedPath);

		// 読み込み済みなら、同じモデルへの共有参照を返す
		const auto found = impl_->cache.find(key);

		if (found != impl_->cache.end()) {
			ModelHandle handle;
			handle.model_ = found->second;
			handle.graphics_ = &impl_->graphics;
			return handle;
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
		impl_->cache.emplace(key, model);

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
