#include "assets/AssetSources.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {
/// <summary>
/// フォルダの参照を字句的に正規化し、通常のWindowsファイル名とパスの種類を確認する
/// </summary>
/// <param name="directory">入力パス。存在しないフォルダも保持する</param>
/// <returns>正規化したパス。空・NUL・ドライブ相対指定や不正な名前は例外</returns>
std::filesystem::path Normalize(std::filesystem::path directory) {
	if (directory.empty() ||
	    directory.native().find(L'\0') != std::wstring::npos ||
	    (directory.has_root_path() && !directory.is_absolute())) {
		throw std::invalid_argument("Choose a valid Assets folder.");
	}

	directory = directory.lexically_normal();
	if (!directory.has_filename() && directory != directory.root_path()) {
		directory = directory.parent_path();
	}
	for (const auto &part : directory.relative_path()) {
		if (!PathUtils::IsValidFileName(part)) {
			throw std::invalid_argument("An Assets folder name is invalid.");
		}
	}
	return directory;
}

/// <summary>
/// 字句的に正規化した参照が、取得元の配下にあるか確認する
/// </summary>
/// <param name="directory">取得元の絶対パス</param>
/// <param name="path">シーンの参照先</param>
/// <returns>true:取得元の配下 false:相対指定・取得元外・無効な参照</returns>
bool Contains(const std::filesystem::path &directory,
              const std::filesystem::path &path) {
	if (!path.is_absolute()) {
		return false;
	}
	try {
		PathUtils::RelativeInside(directory, path.lexically_normal());
		return true;
	} catch (const std::exception &) {
		return false;
	}
}
} // namespace

void AssetSources::Select(std::filesystem::path directory) {
	directory = Normalize(std::move(directory));
	if (directory.is_absolute()) {
		Register(directory);
	} else {
		PathUtils::RelativeInside(std::filesystem::path(L"resources"),
		                          directory);
	}
	directory_.swap(directory);
}

void AssetSources::Register(std::filesystem::path directory) {
	directory = Normalize(std::move(directory));
	if (!directory.is_absolute()) {
		throw std::invalid_argument(
		    "External asset sources need absolute paths.");
	}
	if (std::ranges::any_of(directories_, [&](const auto &existing) {
		    return Contains(existing, directory) &&
		           Contains(directory, existing);
	    })) {
		return;
	}
	directories_.push_back(std::move(directory));
}

const std::filesystem::path &AssetSources::GetDirectory() const {
	return directory_;
}

const std::vector<std::filesystem::path> &AssetSources::GetDirectories() const {
	return directories_;
}

std::vector<std::filesystem::path>
AssetSources::ForScene(const Scene &scene) const {
	std::vector<std::filesystem::path> result;
	const bool transitions =
	    std::ranges::any_of(scene.GetObjects(), [](const auto &entry) {
		    return entry.second.triggerActions &&
		           std::ranges::any_of(
		               entry.second.triggerActions->actions,
		               [](const auto &action) {
			               return action.operation ==
			                          TriggerAction::Operation::LOAD_SCENE &&
			                      !action.scenePath.empty();
		               });
	    });
	for (const auto &directory : directories_) {
		const bool used =
		    std::ranges::any_of(scene.GetObjects(), [&](const auto &entry) {
			    const auto &object = entry.second;
			    if (object.triggerActions &&
			        std::ranges::any_of(
			            object.triggerActions->actions,
			            [&](const auto &action) {
				            return action.operation ==
				                       TriggerAction::Operation::LOAD_SCENE &&
				                   !action.scenePath.empty() &&
				                   Contains(directory, action.scenePath);
			            }))
				    return true;
			    if (object.image && !object.image->texture.empty() &&
			        Contains(directory, object.image->texture))
				    return true;
			    if (object.particleEmitter &&
			        !object.particleEmitter->texture.empty() &&
			        Contains(directory, object.particleEmitter->texture))
				    return true;
			    if (object.script &&
			        std::ranges::any_of(
			            object.script->scenes, [&](const auto &entry) {
				            return !entry.second.empty() &&
				                   Contains(directory, entry.second);
			            }))
				    return true;
			    if (object.script &&
			        std::ranges::any_of(
			            object.script->prefabs, [&](const auto &entry) {
				            return !entry.second.empty() &&
				                   Contains(directory, entry.second);
			            }))
				    return true;
			    if (object.script && !object.script->asset.empty() &&
			        Contains(directory, object.script->asset))
				    return true;
			    if (object.audioSource &&
			        !object.audioSource->clipPath.empty() &&
			        Contains(directory, object.audioSource->clipPath)) {
				    return true;
			    }
			    if (object.HasMesh() &&
			        Contains(directory, object.mesh->modelPath)) {
				    return true;
			    }
			    return object.meshRenderer &&
			           std::ranges::any_of(object.meshRenderer->textures.slots,
			                               [&](const auto &binding) {
				                               return Contains(directory,
				                                               binding.second);
			                               });
		    });
		std::error_code directoryError;
		if (used || (transitions && std::filesystem::is_directory(
		                                directory, directoryError))) {
			result.push_back(directory);
		}
	}
	return result;
}
