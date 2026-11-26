#include "scene/PrefabSerializer.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include "scene/SceneAssetReferences.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneObjectReferences.h"
#include "scene/SceneSerializer.h"
#include <format>
#include <fstream>
#include <iterator>
#include <json.hpp>
#include <stdexcept>
#include <utility>

namespace {
using Json = nlohmann::json;

/// <summary>
/// 全物体が1つの階層を構成し、範囲外を参照しないことを確認する
/// </summary>
/// <param name="scene">検証するPrefab</param>
/// <returns>根のID。無効な階層や参照は例外</returns>
SceneObjectId ValidateHierarchy(const Scene &scene) {
	SceneObjectId root = 0;
	std::vector<SceneObjectId> ids;

	for (const auto &[id, object] : scene.GetObjects()) {
		if (object.prefabLink) {
			throw std::runtime_error(
			    "Prefab assets must contain unpacked object data.");
		}

		ids.push_back(id);

		if (!object.GetParentId()) {
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
	SceneAssetReferences::Rewrite(scene, [&](const auto &path) {
		const auto absolute = saving ? PathUtils::ResolveFromExecutable(path)
		                             : (base / path).lexically_normal();
		const auto relative = absolute.lexically_relative(base);
		return saving && !relative.empty() ? relative : absolute;
	});
}

} // namespace

bool PrefabSerializer::IsPrefabPath(const std::filesystem::path &path) {
	return PathUtils::HasExtension(path, ".json") &&
	       PathUtils::HasExtension(path.stem(), ".prefab");
}

bool PrefabSerializer::Load(const std::filesystem::path &path, Scene &result,
                            std::string &error, SceneObjectId *nextSourceId) {
	try {
		if (!IsPrefabPath(path))
			throw std::invalid_argument("Choose a .prefab.json asset.");
		const auto source = PathUtils::ResolveFromExecutable(path);
		std::ifstream file(source, std::ios::binary);
		if (!file)
			throw std::runtime_error("Could not open the Prefab asset.");
		file.exceptions(std::ios::badbit);
		const std::string text{std::istreambuf_iterator<char>(file),
		                       std::istreambuf_iterator<char>()};
		return Deserialize(text, source, result, error, nextSourceId);
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool PrefabSerializer::Deserialize(std::string_view text,
                                   const std::filesystem::path &path,
                                   Scene &result, std::string &error,
                                   SceneObjectId *nextSourceId) {
	try {
		if (!IsPrefabPath(path))
			throw std::invalid_argument("Choose a .prefab.json asset.");
		const auto source = PathUtils::ResolveFromExecutable(path);
		const auto document = Json::parse(text);
		if (document.at("format") != "EnginePrefab" ||
		    !document.at("version").is_number_integer() ||
		    (document.at("version") != 1 && document.at("version") != 2) ||
		    !document.at("root").is_number_unsigned()) {
			throw std::runtime_error("Unsupported Prefab format.");
		}
		Scene candidate;
		if (!SceneSerializer::Deserialize(candidate,
		                                  document.at("scene").dump()) ||
		    ValidateHierarchy(candidate) !=
		        document.at("root").get<SceneObjectId>()) {
			throw std::runtime_error(
			    "The Prefab root or object settings are invalid.");
		}
		RewriteReferences(candidate, source.parent_path(), false);
		ValidateHierarchy(candidate);
		const auto minimum = candidate.GetObjects().rbegin()->first + 1;
		SceneObjectId counter = minimum;

		if (document.at("version") == 2) {
			if (!document.at("nextSourceId").is_number_unsigned() ||
			    document.at("nextSourceId") < minimum) {
				throw std::runtime_error(
				    "Prefab source ID counter is invalid.");
			}

			counter = document.at("nextSourceId").get<SceneObjectId>();
		}

		result.ReplaceObjects(candidate.GetObjects());

		if (nextSourceId) {
			*nextSourceId = counter;
		}

		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool PrefabSerializer::Serialize(const Scene &source, SceneObjectId root,
                                 const std::filesystem::path &path,
                                 std::string &text, std::string &error,
                                 SceneObjectId nextSourceId) {
	try {
		if (!IsPrefabPath(path) || ValidateHierarchy(source) != root) {
			throw std::runtime_error("Prefab root or destination is invalid.");
		}

		const auto minimum = source.GetObjects().rbegin()->first + 1;

		if (!nextSourceId) {
			nextSourceId = minimum;
		}

		if (nextSourceId < minimum) {
			throw std::runtime_error(
			    "Prefab source ID counter must be greater than existing IDs.");
		}

		Scene candidate;
		candidate.ReplaceObjects(source.GetObjects());
		RewriteReferences(candidate,
		                  PathUtils::ResolveFromExecutable(path).parent_path(),
		                  true);
		std::string data;

		if (!SceneSerializer::Serialize(candidate, data)) {
			throw std::runtime_error("Could not encode the Prefab references.");
		}

		const Json document{{"format", "EnginePrefab"},
		                    {"version", 2},
		                    {"root", root},
		                    {"nextSourceId", nextSourceId},
		                    {"scene", Json::parse(data)}};
		auto result = document.dump(2);
		text.swap(result);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool PrefabSerializer::Validate(const Scene &scene, SceneObjectId &root,
                                std::string &error) {
	try {
		const auto result = ValidateHierarchy(scene);
		root = result;
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
