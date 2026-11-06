#include "assets/PrefabAsset.h"

#include "assets/SceneAssetValidation.h"
#include "history/EditorHistory.h"
#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneObjectReferences.h"
#include "scene/SceneObjects.h"
#include "scene/SceneSerializer.h"
#include "selection/EditorClipboard.h"
#include <array>
#include <format>
#include <fstream>
#include <iterator>
#include <json.hpp>
#include <map>
#include <stdexcept>
#include <utility>

namespace {
using Json = nlohmann::json;

/// <summary>
/// 全物体が1つの階層を構成し、範囲外を参照しないことを確認する
/// </summary>
/// <param name="scene">検証するPrefab</param>
/// <returns>根のID。無効な階層や参照は例外</returns>
SceneObjectId Validate(const Scene &scene) {
	SceneObjectId root = 0;
	std::vector<SceneObjectId> ids;
	for (const auto &[id, object] : scene.GetObjects()) {
		ids.push_back(id);
		if (!object.parentId) {
			if (root) {
				throw std::runtime_error(
				    "A Prefab must have exactly one root object.");
			}
			root = id;
		}
	}
	std::vector<SceneObjectId> subtree;
	if (!root || !SceneHierarchy::GetSubtree(scene, root, subtree) ||
	    subtree.size() != ids.size()) {
		throw std::runtime_error(
		    "The Prefab hierarchy is empty or disconnected.");
	}
	for (const auto &[id, object] : scene.GetObjects()) {
		if (SceneObjectReferences::HasExternal(object, ids)) {
			throw std::runtime_error(
			    std::format("Object '{}' (ID {}) references an object outside "
			                "the Prefab. Include that object under the "
			                "selected root or clear the reference.",
			                object.name, id));
		}
	}
	std::string text;
	if (!SceneSerializer::Serialize(scene, text)) {
		throw std::runtime_error(
		    "The Prefab contains invalid object settings.");
	}
	return root;
}

/// <summary>
/// 素材の直接参照だけを、保存先基準または読み込み後の絶対パスへ変換する
/// </summary>
/// <param name="scene">階層を変更せず参照を更新するシーン</param>
/// <param name="base">Prefabファイルの絶対フォルダ</param>
/// <param name="saving">true:実行ファイル基準から保存先基準へ変換 false:保存先基準を絶対パスへ変換</param>
void RewriteReferences(Scene &scene, const std::filesystem::path &base,
                       bool saving) {
	const auto rewrite = [&](std::filesystem::path &path) {
		if (path.empty()) {
			return;
		}
		const auto absolute = saving ? PathUtils::ResolveFromExecutable(path)
		                             : (base / path).lexically_normal();
		const auto relative = absolute.lexically_relative(base);
		path = saving && !relative.empty() ? relative : absolute;
	};
	for (const auto &[id, value] : scene.GetObjects()) {
		(void)value;
		auto &object = *scene.FindObject(id);
		if (object.mesh) {
			rewrite(object.mesh->modelPath);
		}
		if (object.meshRenderer) {
			for (auto &[slot, path] : object.meshRenderer->textures.slots) {
				(void)slot;
				rewrite(path);
			}
		}
		if (object.audioSource) {
			rewrite(object.audioSource->clipPath);
		}
	}
}
} // namespace

bool PrefabAsset::IsPrefabPath(const std::filesystem::path &path) {
	return PathUtils::HasExtension(path, ".json") &&
	       PathUtils::HasExtension(path.stem(), ".prefab");
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
			objects.emplace(copy.GetSourceIds()[i], copy.GetObjects()[i]);
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
		RewriteReferences(candidate, destination.parent_path(), true);
		std::string text;
		if (!SceneSerializer::Serialize(candidate, text)) {
			throw std::runtime_error("Could not encode the Prefab references.");
		}
		const Json document{{"format", "EnginePrefab"},
		                    {"version", 1},
		                    {"root", root},
		                    {"scene", Json::parse(text)}};
		return FileUtils::WriteTextAtomically(destination, document.dump(2),
		                                      error, false);
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool PrefabAsset::Load(const std::filesystem::path &path, Scene &result,
                       std::string &error) {
	try {
		if (!IsPrefabPath(path)) {
			throw std::invalid_argument("Choose a .prefab.json asset.");
		}
		const auto source = PathUtils::ResolveFromExecutable(path);
		std::ifstream file(source, std::ios::binary);
		if (!file) {
			throw std::runtime_error("Could not open the Prefab asset.");
		}
		file.exceptions(std::ios::badbit);
		const auto document =
		    Json::parse(std::string{std::istreambuf_iterator<char>(file),
		                            std::istreambuf_iterator<char>()});
		if (document.at("format") != "EnginePrefab" ||
		    !document.at("version").is_number_integer() ||
		    document.at("version") != 1 ||
		    !document.at("root").is_number_unsigned()) {
			throw std::runtime_error("Unsupported Prefab format.");
		}
		Scene candidate;
		if (!SceneSerializer::Deserialize(candidate,
		                                  document.at("scene").dump()) ||
		    Validate(candidate) != document.at("root").get<SceneObjectId>()) {
			throw std::runtime_error(
			    "The Prefab root or object settings are invalid.");
		}
		RewriteReferences(candidate, source.parent_path(), false);
		Validate(candidate);
		result.ReplaceObjects(candidate.GetObjects());
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
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
