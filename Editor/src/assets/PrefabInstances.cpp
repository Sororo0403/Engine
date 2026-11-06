#include "assets/PrefabInstances.h"

#include "assets/PrefabAsset.h"
#include "assets/SceneAssetValidation.h"
#include "history/EditorHistory.h"
#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneObjectReferences.h"
#include "scene/SceneSerializer.h"
#include <algorithm>
#include <format>
#include <json.hpp>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
using IdMap = std::map<SceneObjectId, SceneObjectId>;

/// <summary>
/// 基準値用に、Prefabリンクを含まないシーンをJSONへ変換する
/// </summary>
/// <param name="scene">変換するシーン</param>
/// <returns>検証済みJSON。無効なデータは例外</returns>
Json Document(const Scene &scene) {
	std::string text;
	if (!SceneSerializer::Serialize(scene, text)) {
		throw std::runtime_error("Could not encode the Prefab scene.");
	}
	return Json::parse(text);
}

/// <summary>
/// アセットの単独の根を取得し、外部物体への参照を拒否する
/// </summary>
/// <param name="source">リンクを含まないアセット階層</param>
/// <returns>根のID。設定や階層が無効な場合は例外</returns>
SceneObjectId RootOf(const Scene &source) {
	SceneObjectId root = 0;
	std::vector<SceneObjectId> ids;
	for (const auto &[id, object] : source.GetObjects()) {
		ids.push_back(id);
		if (object.prefabLink) {
			throw std::runtime_error(
			    "Unpack nested Prefabs before saving this asset.");
		}
		if (!object.parentId) {
			if (root) {
				throw std::runtime_error("A Prefab must have one root.");
			}
			root = id;
		}
	}
	std::vector<SceneObjectId> subtree;
	if (!root || !SceneHierarchy::GetSubtree(source, root, subtree) ||
	    subtree.size() != ids.size()) {
		throw std::runtime_error("The Prefab hierarchy is invalid.");
	}
	for (const auto &[id, object] : source.GetObjects()) {
		(void)id;
		if (SceneObjectReferences::HasExternal(object, ids)) {
			throw std::runtime_error(
			    "A Prefab asset cannot reference objects outside its root.");
		}
	}
	Document(source);
	return root;
}

/// <summary>
/// 素材の存在・内容を確認する。編集中のシーンへはまだ追加しない
/// </summary>
/// <param name="source">読み込んだ最新アセット</param>
void ValidateResources(const Scene &source) {
	const auto issues = SceneAssetValidation::Inspect(source);
	if (!issues.empty()) {
		throw std::runtime_error(issues.front().message);
	}
}

/// <summary>
/// 所属する根の基準値を読み込み、保存した対応表と元の根を確認する
/// </summary>
/// <param name="link">根が保持するリンク</param>
/// <param name="result">読み込む基準シーン</param>
void ReadBaseline(const PrefabLinkComponent &link, Scene &result) {
	if (!link.IsValid() || link.asset.empty() ||
	    !SceneSerializer::Deserialize(result, link.baseline) ||
	    RootOf(result) != link.source) {
		throw std::runtime_error("The Prefab baseline is invalid. Unpack the "
		                         "instance to keep its objects.");
	}
}

/// <summary>
/// アセットを基準の配置へ平行移動し、世界座標の復帰設定も合わせる
/// </summary>
/// <param name="scene">変更するアセット用コピー</param>
/// <param name="root">根の元ID</param>
/// <param name="position">基準にするワールド位置</param>
void Align(Scene &scene, SceneObjectId root,
           const DirectX::XMFLOAT3 &position) {
	const auto old = scene.FindObject(root)->transform.translation;
	const DirectX::XMFLOAT3 delta{position.x - old.x, position.y - old.y,
	                              position.z - old.z};
	scene.FindObject(root)->transform.translation = position;
	for (const auto &[id, value] : scene.GetObjects()) {
		(void)value;
		if (auto &respawn = scene.FindObject(id)->respawn; respawn) {
			respawn->minimumY += delta.y;
			respawn->position = {respawn->position.x + delta.x,
			                     respawn->position.y + delta.y,
			                     respawn->position.z + delta.z};
		}
	}
}

/// <summary>
/// 所属情報を除いた物体の値を、比較・合成に使うJSONへ変換する
/// </summary>
/// <param name="value">変換する物体</param>
/// <param name="id">JSONへ保持するID</param>
/// <returns>親の存在に依存しない物体データ</returns>
Json ObjectData(SceneObject value, SceneObjectId id) {
	const auto parent = value.parentId;
	value.parentId = 0;
	value.prefabLink.reset();
	Scene single;
	single.ReplaceObjects({{1, std::move(value)}});
	auto object = Document(single).at("objects").at(0);
	object["id"] = id;
	object["parent"] = parent;
	return object;
}

/// <summary>
/// 元IDの物体を、配置先IDへ付け替えた比較用データにする
/// </summary>
/// <param name="source">基準または最新アセットの物体</param>
/// <param name="id">元ID</param>
/// <param name="map">元IDから配置先IDへの対応。0は配置から削除済み</param>
/// <returns>配置先のIDを使う物体データ</returns>
Json Mapped(SceneObject source, SceneObjectId id, const IdMap &map) {
	if (source.parentId) {
		source.parentId = map.at(source.parentId);
	}
	SceneObjectReferences::Remap(source, map);
	return ObjectData(std::move(source), map.at(id));
}

/// <summary>
/// 個別に変えた値を残し、基準から変えていない項目だけを最新値へ更新する
/// </summary>
/// <param name="base">更新前の値。未設定は項目なし</param>
/// <param name="current">配置後に編集した値</param>
/// <param name="latest">アセットの新しい値</param>
/// <returns>合成した値。配列と任意コンポーネントの追加・削除は一単位として扱う</returns>
std::optional<Json> Merge(const std::optional<Json> &base,
                          const std::optional<Json> &current,
                          const std::optional<Json> &latest) {
	if (current == base) {
		return latest;
	}
	if (!base || !current || !latest || !base->is_object() ||
	    !current->is_object() || !latest->is_object()) {
		return current;
	}
	std::set<std::string> keys;
	for (const auto *value : {&*base, &*current, &*latest}) {
		for (const auto &[key, field] : value->items()) {
			(void)field;
			keys.insert(key);
		}
	}
	Json result = Json::object();
	const auto field = [](const Json &value,
	                      const std::string &key) -> std::optional<Json> {
		return value.contains(key) ? std::optional{value.at(key)}
		                           : std::nullopt;
	};
	for (const auto &key : keys) {
		if (const auto value = Merge(field(*base, key), field(*current, key),
		                             field(*latest, key))) {
			result[key] = *value;
		}
	}
	return result;
}

/// <summary>
/// 変更対象外のPrimary Cameraを保ち、新しい配置でPrimaryを重複させない
/// </summary>
/// <param name="scene">更新する候補</param>
/// <param name="affected">今回の配置または更新対象</param>
void PreservePrimary(Scene &scene, const std::set<SceneObjectId> &affected) {
	bool primary =
	    std::ranges::any_of(scene.GetObjects(), [&](const auto &entry) {
		    return !affected.contains(entry.first) && entry.second.camera &&
		           entry.second.camera->primary;
	    });
	for (const auto id : affected) {
		auto *object = scene.FindObject(id);
		if (object && object->camera && object->camera->primary) {
			object->camera->primary = !primary;
			primary = true;
		}
	}
}

/// <summary>
/// 1配置を更新し、個別削除・追加とアセット側の構造変更をまとめて扱う
/// </summary>
/// <param name="scene">作業中のシーン</param>
/// <param name="root">配置の根</param>
/// <param name="latest">素材を検証済みの最新アセット</param>
/// <param name="revert">個別編集を取り消すか</param>
/// <param name="adopt">アセットへ保存したローカル追加物体の対応。通常更新は空</param>
void RefreshOne(Scene &scene, SceneObjectId root, const Scene &latest,
                bool revert, const IdMap &adopt) {
	const auto originalRoot = *scene.FindObject(root);
	const auto link = *originalRoot.prefabLink;
	const IdMap baselineMap(link.objects.begin(), link.objects.end());
	Scene base, next;
	ReadBaseline(link, base);
	if (RootOf(latest) != link.source) {
		throw std::runtime_error("The Prefab root identity changed. Unpack and "
		                         "recreate this instance.");
	}
	next.ReplaceObjects(latest.GetObjects());
	auto anchor = base.FindObject(link.source)->transform.translation;
	if (revert) {
		SceneWorldTransform world;
		if (!SceneHierarchy::GetWorldTransform(scene, root, world)) {
			throw std::runtime_error("The Prefab root transform is invalid.");
		}
		anchor = world.GetPosition();
	}
	Align(next, link.source, anchor);

	IdMap current;
	for (const auto &[id, object] : scene.GetObjects()) {
		if (object.prefabLink && object.prefabLink->root == root) {
			current.emplace(object.prefabLink->source, id);
		}
	}
	std::set<SceneObjectId> originalMembers;
	for (const auto &[source, id] : current) {
		(void)source;
		originalMembers.insert(id);
	}
	IdMap mappings;
	for (const auto &[source, value] : next.GetObjects()) {
		(void)value;
		if (const auto found = current.find(source); found != current.end()) {
			mappings[source] = found->second;
		} else if (const auto adopted = adopt.find(source);
		           adopted != adopt.end() && scene.Contains(adopted->second)) {
			mappings[source] = adopted->second;
			current[source] = adopted->second;
		} else if (!revert && base.Contains(source)) {
			mappings[source] = link.GetInstance(source);
		} else {
			mappings[source] = 0;
		}
	}
	std::vector<SceneObjectId> order;
	if (!SceneHierarchy::GetEvaluationOrder(next, order)) {
		throw std::runtime_error("The asset hierarchy is invalid.");
	}
	std::set<SceneObjectId> eligible;
	for (const auto source : order) {
		if (current.contains(source)) {
			eligible.insert(source);
			continue;
		}
		if (!revert && base.Contains(source)) {
			continue;
		}
		const auto parent = next.FindObject(source)->parentId;
		if (parent && !eligible.contains(parent)) {
			continue;
		}
		mappings[source] = scene.CreateObject("Prefab Object");
		eligible.insert(source);
	}
	IdMap references = mappings;
	for (auto &[source, id] : references) {
		if (!eligible.contains(source)) {
			id = 0;
		}
	}

	std::map<SceneObjectId, Json> objects;
	std::map<SceneObjectId, PrefabLinkComponent> previousLinks;
	for (const auto &[id, object] : scene.GetObjects()) {
		objects.emplace(id, ObjectData(object, id));
		if (object.prefabLink) {
			previousLinks.emplace(id, *object.prefabLink);
		}
	}
	std::set<SceneObjectId> remove;
	for (const auto &[source, id] : current) {
		if (next.Contains(source)) {
			continue;
		}
		if (revert || (base.Contains(source) &&
		               objects.at(id) == Mapped(*base.FindObject(source),
		                                        source, baselineMap))) {
			remove.insert(id);
		}
	}
	if (revert) {
		for (const auto id : originalMembers) {
			std::vector<SceneObjectId> subtree;
			if (!SceneHierarchy::GetSubtree(scene, id, subtree)) {
				throw std::runtime_error(
				    "The current instance hierarchy is invalid.");
			}
			for (const auto child : subtree) {
				if (!originalMembers.contains(child)) {
					remove.insert(child);
				}
			}
		}
	}
	std::set<SceneObjectId> affected;
	for (const auto source : eligible) {
		const auto id = mappings.at(source);
		const auto value = Mapped(*next.FindObject(source), source, references);
		Json merged = value;
		if (!revert && current.contains(source) && base.Contains(source)) {
			merged =
			    *Merge(Mapped(*base.FindObject(source), source, baselineMap),
			           objects.at(id), value);
		}
		if (source == link.source) {
			merged["parent"] = originalRoot.parentId;
			merged["transform"] =
			    ObjectData(originalRoot, root).at("transform");
		}
		objects[id] = std::move(merged);
		remove.erase(id);
		affected.insert(id);
	}
	if (!revert) {
		// 残すローカル子を持つ古い親は、通常物体として残して階層を壊さない。
		bool changed;
		do {
			changed = false;
			for (const auto &[id, object] : objects) {
				if (!remove.contains(id)) {
					changed |=
					    remove.erase(
					        object.at("parent").get<SceneObjectId>()) != 0;
				}
			}
		} while (changed);
	}
	for (const auto id : remove) {
		objects.erase(id);
	}
	auto document = Document(scene);
	document["objects"] = Json::array();
	for (const auto &[id, object] : objects) {
		(void)id;
		document["objects"].push_back(object);
	}
	Scene result;
	if (!SceneSerializer::Deserialize(result, document.dump())) {
		throw std::runtime_error(
		    "Prefab update conflicts with the current hierarchy or transforms. "
		    "The scene was kept.");
	}
	for (const auto &[id, previous] : previousLinks) {
		if (result.Contains(id) && previous.root != root &&
		    result.Contains(previous.root)) {
			result.FindObject(id)->prefabLink = previous;
		}
	}
	for (const auto source : eligible) {
		const auto id = mappings.at(source);
		result.FindObject(id)->prefabLink =
		    PrefabLinkComponent{root, source, {}, {}, {}};
	}
	auto &updated = *result.FindObject(root)->prefabLink;
	updated.asset = link.asset;
	updated.baseline = Document(next).dump(2);
	updated.objects.assign(mappings.begin(), mappings.end());
	const std::vector<SceneObjectId> removed(remove.begin(), remove.end());
	for (const auto &[id, object] : result.GetObjects()) {
		(void)object;
		SceneObjectReferences::ClearTargets(*result.FindObject(id), removed);
	}
	PreservePrimary(result, affected);
	Document(result);
	scene.ReplaceObjects(result.GetObjects());
}

/// <summary>
/// 同じアセットの配置を、1つの検証済み履歴操作で更新する
/// </summary>
/// <param name="history">編集対象</param>
/// <param name="root">指定した根</param>
/// <param name="revert">個別編集を取り消すか</param>
/// <param name="all">同じアセットの全配置を含めるか</param>
/// <param name="adopt">指定した根で追加済みの元ID対応</param>
/// <returns>true:成功 false:処理失敗。例外は呼び出し側で扱う</returns>
bool RefreshGroup(EditorHistory &history, SceneObjectId root, bool revert,
                  bool all, const IdMap &adopt = {}) {
	const auto path = history.GetScene().FindObject(root)->prefabLink->asset;
	std::vector<SceneObjectId> roots;
	for (const auto &[id, object] : history.GetScene().GetObjects()) {
		if (object.prefabLink && object.prefabLink->root == id &&
		    (id == root ||
		     (all && PathUtils::IsSameFile(object.prefabLink->asset, path)))) {
			roots.push_back(id);
		}
	}
	std::map<SceneObjectId, std::map<SceneObjectId, SceneObject>> latest;
	for (const auto id : roots) {
		Scene source;
		std::string error;
		if (!PrefabAsset::Load(
		        history.GetScene().FindObject(id)->prefabLink->asset, source,
		        error)) {
			throw std::runtime_error(error);
		}
		ValidateResources(source);
		latest.emplace(id, source.GetObjects());
	}
	return history.ApplySceneChanges(
	    [&](Scene &candidate) {
		    for (const auto id : roots) {
			    Scene source;
			    source.ReplaceObjects(latest.at(id));
			    RefreshOne(candidate, id, source, revert,
			               id == root ? adopt : IdMap{});
		    }
		    return true;
	    },
	    revert ? "Revert Prefab Overrides"
	    : all  ? "Refresh Prefab Instances"
	           : "Refresh Prefab");
}
} // namespace

SceneObjectId PrefabInstances::GetRoot(const Scene &scene,
                                       SceneObjectId member) {
	const auto *object = scene.FindObject(member);
	if (!object || !object->prefabLink) {
		return 0;
	}
	const auto root = object->prefabLink->root;
	const auto *owner = scene.FindObject(root);
	return owner && owner->prefabLink && owner->prefabLink->root == root &&
	               !owner->prefabLink->asset.empty()
	           ? root
	           : 0;
}

std::vector<SceneObjectId>
PrefabInstances::Instantiate(const std::filesystem::path &asset,
                             const Scene &placed, EditorHistory &history,
                             std::string &error) {
	std::vector<SceneObjectId> ids;
	try {
		error.clear();
		if (history.IsEditing()) {
			throw std::runtime_error(
			    "Finish the current edit before placing a Prefab.");
		}
		const auto sourceRoot = RootOf(placed);
		ValidateResources(placed);
		const auto path = PathUtils::ResolveFromExecutable(asset);
		if (!PrefabAsset::IsPrefabPath(path)) {
			throw std::runtime_error("Choose a Prefab asset.");
		}
		const auto baseline = Document(placed).dump(2);
		const bool applied = history.ApplySceneChanges(
		    [&](Scene &candidate) {
			    IdMap map;
			    for (const auto &[source, object] : placed.GetObjects()) {
				    auto initial = object;
				    initial.parentId = 0;
				    const auto id = candidate.CreateObject(std::move(initial));
				    map[source] = id;
				    ids.push_back(id);
			    }
			    const auto root = map.at(sourceRoot);
			    for (const auto &[source, value] : placed.GetObjects()) {
				    auto object = value;
				    if (object.parentId) {
					    object.parentId = map.at(object.parentId);
				    }
				    SceneObjectReferences::Remap(object, map);
				    object.prefabLink =
				        PrefabLinkComponent{root, source, {}, {}, {}};
				    *candidate.FindObject(map.at(source)) = std::move(object);
			    }
			    auto &link = *candidate.FindObject(root)->prefabLink;
			    link.asset = path;
			    link.baseline = baseline;
			    link.objects.assign(map.begin(), map.end());
			    PreservePrimary(
			        candidate, std::set<SceneObjectId>{ids.begin(), ids.end()});
			    return true;
		    },
		    "Create Prefab Instance");
		if (!applied) {
			throw std::runtime_error("Could not create the linked Prefab "
			                         "instance. The scene was kept.");
		}
		return ids;
	} catch (const std::exception &exception) {
		error = exception.what();
		return {};
	}
}

bool PrefabInstances::Refresh(EditorHistory &history, SceneObjectId member,
                              std::string &error, bool revert,
                              bool allInstances) {
	try {
		error.clear();
		const auto root = GetRoot(history.GetScene(), member);
		if (!root || history.IsEditing()) {
			throw std::runtime_error(
			    "Select a linked Prefab and finish the current edit first.");
		}
		if (!RefreshGroup(history, root, revert, allInstances)) {
			throw std::runtime_error(
			    "Could not record the Prefab update. The scene was kept.");
		}
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool PrefabInstances::Unpack(EditorHistory &history, SceneObjectId member,
                             std::string &error) {
	try {
		error.clear();
		const auto root = GetRoot(history.GetScene(), member);
		if (!root || history.IsEditing()) {
			throw std::runtime_error(
			    "Select a linked Prefab and finish the current edit first.");
		}
		if (!history.ApplySceneChanges(
		        [&](Scene &candidate) {
			        for (const auto &[id, value] : candidate.GetObjects()) {
				        if (value.prefabLink &&
				            value.prefabLink->root == root) {
					        candidate.FindObject(id)->prefabLink.reset();
				        }
			        }
			        return true;
		        },
		        "Unpack Prefab")) {
			throw std::runtime_error(
			    "Could not unpack the Prefab. The scene was kept.");
		}
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool PrefabInstances::SaveToAsset(EditorHistory &history, SceneObjectId member,
                                  std::string &error) {
	bool saved = false;
	try {
		error.clear();
		const auto root = GetRoot(history.GetScene(), member);
		if (!root || history.IsEditing()) {
			throw std::runtime_error(
			    "Select a linked Prefab and finish the current edit first.");
		}
		const auto &scene = history.GetScene();
		const auto link = *scene.FindObject(root)->prefabLink;
		Scene base, disk;
		ReadBaseline(link, base);
		SceneObjectId counter = 0;
		if (!PrefabAsset::Load(link.asset, disk, error, &counter)) {
			return false;
		}
		if (RootOf(disk) != link.source) {
			throw std::runtime_error("The Prefab root identity changed. "
			                         "Refresh or unpack the instance first.");
		}
		Scene aligned;
		aligned.ReplaceObjects(disk.GetObjects());
		Align(aligned, link.source,
		      base.FindObject(link.source)->transform.translation);
		if (aligned.GetObjects() != base.GetObjects()) {
			throw std::runtime_error(
			    "The Prefab asset changed outside this instance. Refresh "
			    "before saving to the asset.");
		}
		std::vector<SceneObjectId> subtree;
		SceneWorldTransform world;
		if (!SceneHierarchy::GetSubtree(scene, root, subtree) ||
		    !SceneHierarchy::GetWorldTransform(scene, root, world)) {
			throw std::runtime_error(
			    "The current Prefab hierarchy is invalid.");
		}
		IdMap inverse, adopt;
		for (const auto id : subtree) {
			const auto &object = *scene.FindObject(id);
			if (object.prefabLink && object.prefabLink->root != root) {
				throw std::runtime_error(
				    "Unpack nested Prefabs before saving this asset.");
			}
			SceneObjectId source = 0;
			if (object.prefabLink) {
				source = object.prefabLink->source;
			} else {
				if (counter == (std::numeric_limits<SceneObjectId>::max)()) {
					throw std::runtime_error(
					    "The Prefab source ID space is exhausted.");
				}
				source = counter++;
			}
			inverse.emplace(id, source);
			adopt.emplace(source, id);
		}
		std::map<SceneObjectId, SceneObject> objects;
		const auto anchor = base.FindObject(link.source)->transform.translation;
		for (const auto id : subtree) {
			auto object = *scene.FindObject(id);
			object.prefabLink.reset();
			if (SceneObjectReferences::HasExternal(object, subtree)) {
				throw std::runtime_error(
				    "Include external object references under the Prefab root "
				    "before saving.");
			}
			const auto source = inverse.at(id);
			object.parentId = id == root ? 0 : inverse.at(object.parentId);
			SceneObjectReferences::Remap(object, inverse);
			if (id == root) {
				object.transform = disk.FindObject(link.source)->transform;
			}
			if (object.respawn) {
				const auto *old = base.FindObject(source);
				const auto prior = old ? old->respawn : std::nullopt;
				const auto samePosition = [](const auto &a, const auto &b) {
					return a.x == b.x && a.y == b.y && a.z == b.z;
				};
				const auto at = prior && samePosition(prior->position,
				                                      object.respawn->position)
				                    ? anchor
				                    : world.GetPosition();
				object.respawn->position = {object.respawn->position.x - at.x,
				                            object.respawn->position.y - at.y,
				                            object.respawn->position.z - at.z};
				object.respawn->minimumY -=
				    prior && prior->minimumY == object.respawn->minimumY
				        ? anchor.y
				        : world.GetPosition().y;
			}
			objects.emplace(source, std::move(object));
		}
		Scene source;
		source.ReplaceObjects(std::move(objects));
		RootOf(source);
		ValidateResources(source);
		std::string text;
		if (!PrefabAsset::Serialize(source, link.source, link.asset, text,
		                            error, counter) ||
		    !FileUtils::WriteTextAtomically(link.asset, text, error)) {
			return false;
		}
		saved = true;
		if (!RefreshGroup(history, root, false, true, adopt)) {
			throw std::runtime_error(
			    "Could not record the saved Prefab baseline.");
		}
		return true;
	} catch (const std::exception &exception) {
		error = std::string(saved ? "The Prefab file was saved, but instance "
		                            "synchronization failed: "
		                          : "") +
		        exception.what();
		return false;
	}
}
