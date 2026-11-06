#include "assets/PrefabAsset.h"

#include "assets/SceneAssetValidation.h"
#include "history/EditorHistory.h"
#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"
#include "scene/PrefabSerializer.h"
#include "scene/Scene.h"
#include "scene/SceneObjects.h"
#include "selection/EditorClipboard.h"
#include <array>
#include <map>
#include <stdexcept>
#include <utility>

namespace {
/// <summary>
/// Engineと共通の階層検証を行い、編集操作で扱う例外へ変換する
/// </summary>
/// <param name="scene">変更しないPrefab</param>
/// <returns>単独の根のID。無効なら失敗理由を例外にする</returns>
SceneObjectId Validate(const Scene &scene) {
	SceneObjectId root = 0;
	std::string error;
	if (!PrefabSerializer::Validate(scene, root, error))
		throw std::runtime_error(error);
	return root;
}
} // namespace

bool PrefabAsset::IsPrefabPath(const std::filesystem::path &path) {
	return PrefabSerializer::IsPrefabPath(path);
}

bool PrefabAsset::Capture(const Scene &source, SceneObjectId root,
                          Scene &result, std::string &error) {
	try {
		EditorClipboard copy;
		if (!copy.Copy(source, std::array{root})) {
			throw std::runtime_error(
			    "Select one valid root. Its world transform must be "
			    "representable without shear when detached from its parent.");
		}
		std::map<SceneObjectId, SceneObject> objects;
		for (std::size_t i = 0; i < copy.GetSourceIds().size(); ++i) {
			auto object = copy.GetObjects()[i];
			object.prefabLink.reset();
			objects.emplace(copy.GetSourceIds()[i], std::move(object));
		}
		Scene candidate;
		candidate.ReplaceObjects(std::move(objects));
		Validate(candidate);
		const auto origin = candidate.FindObject(root)->transform.translation;
		candidate.FindObject(root)->transform.translation = {};
		for (const auto &[id, value] : candidate.GetObjects()) {
			(void)value;
			if (auto &respawn = candidate.FindObject(id)->respawn; respawn) {
				respawn->minimumY -= origin.y;
				respawn->position = {respawn->position.x - origin.x,
				                     respawn->position.y - origin.y,
				                     respawn->position.z - origin.z};
			}
		}
		Validate(candidate);
		result.ReplaceObjects(candidate.GetObjects());
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool PrefabAsset::Save(const Scene &source, SceneObjectId root,
                       const std::filesystem::path &path, std::string &error) {
	try {
		if (!IsPrefabPath(path) ||
		    !PathUtils::IsValidFileName(path.filename())) {
			throw std::invalid_argument(
			    "Choose a valid .prefab.json file name.");
		}
		const auto destination = PathUtils::ResolveFromExecutable(path);
		Scene candidate;
		if (!Capture(source, root, candidate, error)) {
			return false;
		}
		std::string text;
		if (!Serialize(candidate, root, destination, text, error)) {
			return false;
		}
		return FileUtils::WriteTextAtomically(destination, text, error, false);
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool PrefabAsset::Load(const std::filesystem::path &path, Scene &result,
                       std::string &error, SceneObjectId *nextSourceId) {
	return PrefabSerializer::Load(path, result, error, nextSourceId);
}

std::vector<SceneObjectId> PrefabAsset::Instantiate(const Scene &source,
                                                    EditorHistory &history,
                                                    std::string &error) {
	try {
		if (history.IsEditing()) {
			throw std::runtime_error(
			    "Finish the current edit before placing a Prefab.");
		}
		Validate(source);
		const auto issues = SceneAssetValidation::Inspect(source);
		if (!issues.empty()) {
			throw std::runtime_error(issues.front().message);
		}
		auto ids = SceneObjects::Append(source, history);
		if (ids.empty()) {
			throw std::runtime_error(
			    "Could not create the Prefab objects. The scene was kept.");
		}
		error.clear();
		return ids;
	} catch (const std::exception &exception) {
		error = exception.what();
		return {};
	}
}

bool PrefabAsset::Serialize(const Scene &source, SceneObjectId root,
                            const std::filesystem::path &path,
                            std::string &text, std::string &error,
                            SceneObjectId nextSourceId) {
	return PrefabSerializer::Serialize(source, root, path, text, error,
	                                   nextSourceId);
}
