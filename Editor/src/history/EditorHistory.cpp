#include "history/EditorHistory.h"
#include "scripting/LuaScript.h"

#include "core/Logger.h"
#include "gameplay/SceneCameraUtils.h"
#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include "physics/ColliderUtils.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneObjectReferences.h"
#include "scene/SceneSerializer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <exception>
#include <format>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {
/// <summary>
/// 選択の重複と存在を検証し、合成済みのワールド位置を集める
/// </summary>
/// <param name="scene">変更しないシーン</param>
/// <param name="ids">重複のない既存IDの一覧</param>
/// <param name="positions">成功時だけ置き換えるワールド位置</param>
/// <returns>true:成功 false:対象なし、または選択・変形が無効</returns>
bool ReadWorldPositions(const Scene &scene, std::span<const SceneObjectId> ids,
                        std::map<SceneObjectId, DirectX::XMFLOAT3> &positions) {
	SceneHierarchy::WorldTransforms transforms;

	if (ids.empty() ||
	    !SceneHierarchy::BuildWorldTransforms(scene, transforms)) {
		return false;
	}

	std::map<SceneObjectId, DirectX::XMFLOAT3> result;

	for (const auto id : ids) {
		const auto found = transforms.find(id);

		if (found == transforms.end() ||
		    !result.emplace(id, found->second.GetPosition()).second) {
			return false;
		}
	}

	positions.swap(result);
	return true;
}

/// <summary>
/// 配置に使うワールド座標の軸か確認する
/// </summary>
/// <param name="axis">確認する軸</param>
/// <returns>true:X・Y・Z false:未対応の値</returns>
bool IsPlacementAxis(AlignmentAxis axis) {
	return axis == AlignmentAxis::X || axis == AlignmentAxis::Y ||
	       axis == AlignmentAxis::Z;
}

/// <summary>
/// 配置対象の指定軸を参照する
/// </summary>
/// <param name="value">ワールド位置</param>
/// <param name="axis">有効な軸</param>
/// <returns>指定軸の成分</returns>
float &Coordinate(DirectX::XMFLOAT3 &value, AlignmentAxis axis) {
	return axis == AlignmentAxis::X   ? value.x
	       : axis == AlignmentAxis::Y ? value.y
	                                  : value.z;
}
} // namespace

EditorHistory::EditorHistory(Scene &scene) noexcept : scene_(scene) {
}

SceneObjectId EditorHistory::CreateObject(std::string name,
                                          std::filesystem::path modelPath) {
	CommitEdit();

	SceneObject object;
	object.name = std::move(name);

	if (!modelPath.empty()) {
		object.mesh = MeshComponent{std::move(modelPath)};
		object.meshRenderer = MeshRendererComponent{};
	}

	return CreateObjectWithHistory(std::move(object));
}

bool EditorHistory::DeleteObject(SceneObjectId objectId) {
	return DeleteObjects(std::span<const SceneObjectId>{&objectId, 1});
}

SceneObjectId EditorHistory::DuplicateObject(SceneObjectId objectId) {
	auto createdIds =
	    DuplicateObjects(std::span<const SceneObjectId>{&objectId, 1});

	return createdIds.empty() ? 0 : createdIds.front();
}

bool EditorHistory::SetParent(std::span<const SceneObjectId> ids,
                              SceneObjectId parent, bool keepWorld) {
	CommitEdit();
	std::vector<SceneObjectId> roots;
	if (!SceneHierarchy::GetSelectionRoots(scene_, ids, roots) ||
	    (parent != 0 && !scene_.Contains(parent)))
		return false;
	Scene working;
	working.ReplaceObjects(scene_.GetObjects());
	std::vector<CommandGroup::Command> commands;

	for (const auto id : roots) {
		const auto &before = *scene_.FindObject(id);
		if (before.parentId == parent)
			continue;
		if (!SceneHierarchy::SetParent(working, id, parent, keepWorld))
			return false;
		const auto &after = *working.FindObject(id);
		commands.emplace_back(ParentCommand{id, before.parentId, after.parentId,
		                                    before.transform, after.transform});
	}

	if (commands.empty())
		return true;
	SceneHierarchy::WorldTransforms transforms;
	if (!SceneHierarchy::BuildWorldTransforms(working, transforms))
		return false;
	CommandGroup group{std::move(commands)};
	const auto next = PrepareAppend();
	if (!group.Redo(scene_))
		return false;
	AppendPrepared(Command{std::move(group)}, next,
	               parent == 0 ? "Unparent Objects" : "Parent Objects");
	return true;
}

bool EditorHistory::BeginRename(SceneObjectId objectId) {
	const SceneObject *object = scene_.FindObject(objectId);

	if (!object) {
		return false;
	}

	CommitEdit();

	PendingEdit edit{
	    objectId,
	    object->name,
	};

	pending_ = std::move(edit);
	return true;
}

bool EditorHistory::DeleteObjects(std::span<const SceneObjectId> objectIds) {
	CommitEdit();

	if (objectIds.empty()) {
		return false;
	}

	std::vector<SceneObjectId> expanded;
	if (!SceneHierarchy::ExpandSelection(scene_, objectIds, expanded))
		return false;
	std::set<SceneObjectId> seen(expanded.begin(), expanded.end());

	// Prefabの所有関係と削除後の参照を、最終状態までまとめて検証する。
	if (std::ranges::any_of(scene_.GetObjects(), [](const auto &entry) {
		    return entry.second.prefabLink.has_value();
	    })) {
		return ApplySceneChanges(
		    [&](Scene &candidate) {
			    for (const auto &[id, object] : candidate.GetObjects()) {
				    (void)object;

				    if (!seen.contains(id)) {
					    SceneObjectReferences::ClearTargets(
					        *candidate.FindObject(id), expanded);
				    }
			    }

			    for (auto it = expanded.rbegin(); it != expanded.rend(); ++it) {
				    if (!candidate.DestroyObject(*it))
					    return false;
			    }

			    return true;
		    },
		    objectIds.size() == 1 ? "Delete Object" : "Delete Objects");
	}

	std::vector<CommandGroup::Command> commands;
	commands.reserve(objectIds.size());

	// Undoでは削除物体を先に復元し、その後で残る物体の参照を戻す。
	for (const auto &[id, object] : scene_.GetObjects()) {
		if (seen.contains(id)) {
			continue;
		}

		auto cleared = object;
		SceneObjectReferences::ClearTargets(cleared, expanded);

		if (cleared.script != object.script) {
			commands.emplace_back(
			    ScriptCommand{id, object.script, cleared.script});
		}

		if (cleared.followCamera != object.followCamera) {
			commands.emplace_back(FollowCameraCommand{id, object.followCamera,
			                                          cleared.followCamera});
		}

		if (cleared.animationClip != object.animationClip) {
			commands.emplace_back(AnimationClipCommand{
			    id, object.animationClip, cleared.animationClip});
		}

		if (cleared.triggerActions != object.triggerActions) {
			commands.emplace_back(TriggerActionsCommand{
			    id, object.triggerActions, cleared.triggerActions});
		}
	}

	for (auto it = expanded.rbegin(); it != expanded.rend(); ++it) {
		commands.emplace_back(ObjectCommand{*it, *scene_.FindObject(*it),
		                                    ObjectCommand::Kind::DELETE});
	}

	CommandGroup group{std::move(commands)};

	// シーンを変更する前に、履歴を記録する領域を確保する
	const std::uint64_t next = PrepareAppend();

	if (!group.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(group)}, next,
	               objectIds.size() == 1 ? "Delete Object" : "Delete Objects");
	return true;
}

std::vector<SceneObjectId>
EditorHistory::DuplicateObjects(std::span<const SceneObjectId> objectIds) {
	CommitEdit();

	if (objectIds.empty()) {
		return {};
	}

	std::vector<SceneObjectId> expanded;
	if (!SceneHierarchy::ExpandSelection(scene_, objectIds, expanded))
		return {};

	std::vector<SceneObject> copies;
	copies.reserve(expanded.size());

	for (const SceneObjectId id : expanded) {
		const SceneObject *source = scene_.FindObject(id);

		if (!source) {
			return {};
		}

		SceneObject object = *source;

		if (object.name.empty()) {
			object.name = "Object";
		}

		object.name += " Copy";

		copies.push_back(std::move(object));
	}

	return CreateObjects(std::move(copies), expanded);
}

std::vector<SceneObjectId>
EditorHistory::CreateObjects(std::vector<SceneObject> objects,
                             std::span<const SceneObjectId> sourceIds) {
	CommitEdit();

	if (!sourceIds.empty() && sourceIds.size() != objects.size()) {
		return {};
	}

	if (objects.empty()) {
		return {};
	}

	std::map<SceneObjectId, SceneObjectId> remap;
	for (const auto id : sourceIds) {
		if (id == 0 || id == (std::numeric_limits<SceneObjectId>::max)() ||
		    !remap.emplace(id, 0).second) {
			return {};
		}
	}

	std::vector<SceneObjectId> allowedReferences(sourceIds.begin(),
	                                             sourceIds.end());
	for (const auto &[id, existing] : scene_.GetObjects()) {
		allowedReferences.push_back(id);
	}

	bool hasPrimary =
	    std::ranges::any_of(scene_.GetObjects(), [](const auto &entry) {
		    return entry.second.camera && entry.second.camera->primary;
	    });

	for (std::size_t i = 0; i < objects.size(); ++i) {
		auto &object = objects[i];

		if (object.prefabLink &&
		    std::find(sourceIds.begin(), sourceIds.end(),
		              object.prefabLink->root) == sourceIds.end()) {
			object.prefabLink.reset();
		}

		if (!TransformUtils::IsValid(object.transform) ||
		    (object.camera && !object.camera->IsValid())) {
			return {};
		}

		if (!sourceIds.empty()) {
			// コピー範囲外の対象が削除されていた場合は、全種類の参照を同じ規則で解除する。
			SceneObjectReferences::ClearExternal(object, allowedReferences);
		}

		if (object.camera && object.camera->primary) {
			object.camera->primary = !hasPrimary;
			hasPrimary = true;
		}

		if (object.name.empty()) {
			object.name = "Object";
		}
	}

	std::vector<SceneObjectId> createdIds;
	createdIds.reserve(objects.size());

	std::vector<CommandGroup::Command> commands;
	commands.reserve(objects.size());

	const std::uint64_t next = PrepareAppend();

	const bool created = scene_.ApplyTransaction([&objects, &createdIds,
	                                              &commands, &remap,
	                                              sourceIds](Scene &working) {
		// 全IDが決まってから参照を解決し、入力順に依存しないようにする。
		for (std::size_t i = 0; i < objects.size(); ++i) {
			auto initial = objects[i];
			initial.parentId = 0;
			const auto id = working.CreateObject(std::move(initial));
			createdIds.push_back(id);

			if (!sourceIds.empty()) {
				remap.at(sourceIds[i]) = id;
			}
		}

		for (std::size_t i = 0; i < objects.size(); ++i) {
			auto &object = objects[i];

			if (const auto found = remap.find(object.parentId);
			    found != remap.end()) {
				object.parentId = found->second;
			}
			if (object.parentId != 0 && !working.Contains(object.parentId))
				return false;
			working.FindObject(createdIds[i])->parentId = object.parentId;

			SceneObjectReferences::Remap(object, remap);
			working.FindObject(createdIds[i])->script = object.script;
			working.FindObject(createdIds[i])->prefabLink = object.prefabLink;
			working.FindObject(createdIds[i])->followCamera =
			    object.followCamera;
			working.FindObject(createdIds[i])->triggerActions =
			    object.triggerActions;
		}

		SceneHierarchy::WorldTransforms transforms;
		std::vector<SceneObjectId> order;
		if (!SceneHierarchy::BuildWorldTransforms(working, transforms) ||
		    !SceneHierarchy::GetEvaluationOrder(working, order))
			return false;
		const std::set<SceneObjectId> created(createdIds.begin(),
		                                      createdIds.end());
		for (const auto id : order) {
			if (created.contains(id))
				commands.emplace_back(ObjectCommand{
				    id, *working.FindObject(id), ObjectCommand::Kind::CREATE});
		}

		return true;
	});

	if (!created) {
		return {};
	}

	// 操作後は、確保済みのデータを移動して履歴へ記録する
	AppendPrepared(Command{CommandGroup{std::move(commands)}}, next,
	               createdIds.size() == 1 ? "Create Object" : "Create Objects");

	return createdIds;
}

bool EditorHistory::BeginTransforms(std::span<const SceneObjectId> objectIds) {
	CommitEdit();

	if (objectIds.empty()) {
		return false;
	}

	std::vector<SceneObjectId> roots;
	if (!SceneHierarchy::GetSelectionRoots(scene_, objectIds, roots))
		return false;
	std::vector<TransformSnapshot> snapshots;
	snapshots.reserve(roots.size());

	for (const auto id : roots) {
		const auto *object = scene_.FindObject(id);
		SceneWorldTransform world;
		if (!SceneHierarchy::GetWorldTransform(scene_, id, world))
			return false;
		snapshots.push_back(
		    TransformSnapshot{id, object->transform, object->parentId, world});
	}

	pendingTransforms_ = std::move(snapshots);
	return true;
}

bool EditorHistory::UpdateTransforms(const Transform &beforePivot,
                                     const Transform &afterPivot) {
	if (!pendingTransforms_ || !TransformUtils::IsValid(beforePivot) ||
	    !TransformUtils::IsValid(afterPivot)) {
		return false;
	}

	// ピボットの各軸の倍率を確認し、軸別の拡縮や符号反転を受け入れない
	if (beforePivot.scale.x <= 0.0f || beforePivot.scale.y <= 0.0f ||
	    beforePivot.scale.z <= 0.0f || afterPivot.scale.x <= 0.0f ||
	    afterPivot.scale.y <= 0.0f || afterPivot.scale.z <= 0.0f) {
		return false;
	}

	const double ratioX =
	    static_cast<double>(afterPivot.scale.x) / beforePivot.scale.x;
	const double ratioY =
	    static_cast<double>(afterPivot.scale.y) / beforePivot.scale.y;
	const double ratioZ =
	    static_cast<double>(afterPivot.scale.z) / beforePivot.scale.z;
	const float scaleRatio = static_cast<float>(ratioX);
	const double tolerance = 0.00001 * ratioX;

	if (!std::isfinite(scaleRatio) || scaleRatio <= 0.0f ||
	    std::abs(ratioY - ratioX) > tolerance ||
	    std::abs(ratioZ - ratioX) > tolerance) {
		return false;
	}

	const bool scaled = scaleRatio != 1.0f;

	using namespace DirectX;
	const XMVECTOR beforeRotation =
	    XMQuaternionNormalize(XMLoadFloat4(&beforePivot.rotation));
	const XMVECTOR afterRotation =
	    XMQuaternionNormalize(XMLoadFloat4(&afterPivot.rotation));
	const XMVECTOR deltaRotation = XMQuaternionNormalize(XMQuaternionMultiply(
	    XMQuaternionConjugate(beforeRotation), afterRotation));
	const XMVECTOR beforeCenter = XMLoadFloat3(&beforePivot.translation);
	const XMVECTOR afterCenter = XMLoadFloat3(&afterPivot.translation);

	Transform beforeOrientation;
	beforeOrientation.rotation = beforePivot.rotation;
	Transform afterOrientation;
	afterOrientation.rotation = afterPivot.rotation;
	const bool rotated =
	    !TransformUtils::AreEqual(beforeOrientation, afterOrientation);

	const auto makeEdited = [&](const TransformSnapshot &snapshot) {
		Transform edited = snapshot.before;

		if (scaled) {
			// 元の軸ごとの比率と符号を保ったまま、同じ倍率を適用する
			edited.scale = {
			    snapshot.before.scale.x * scaleRatio,
			    snapshot.before.scale.y * scaleRatio,
			    snapshot.before.scale.z * scaleRatio,
			};
		}

		if (!rotated && !scaled) {
			// 移動だけの場合は、中心との減算・加算による位置の誤差を避ける
			edited.translation = {
			    snapshot.before.translation.x +
			        (afterPivot.translation.x - beforePivot.translation.x),
			    snapshot.before.translation.y +
			        (afterPivot.translation.y - beforePivot.translation.y),
			    snapshot.before.translation.z +
			        (afterPivot.translation.z - beforePivot.translation.z),
			};
			return edited;
		}

		// 開始時点の中心からの距離にも、同じ拡縮・回転を適用する
		XMVECTOR relativePosition = XMVectorSubtract(
		    XMLoadFloat3(&snapshot.before.translation), beforeCenter);

		if (scaled) {
			relativePosition = XMVectorScale(relativePosition, scaleRatio);
		}

		if (rotated) {
			relativePosition = XMVector3Rotate(relativePosition, deltaRotation);
		}

		XMStoreFloat3(&edited.translation,
		              XMVectorAdd(afterCenter, relativePosition));

		if (rotated) {
			const XMVECTOR originalRotation =
			    XMQuaternionNormalize(XMLoadFloat4(&snapshot.before.rotation));
			// DirectXMathでは第1引数の回転の後に第2引数の回転を適用する
			XMStoreFloat4(&edited.rotation,
			              XMQuaternionNormalize(XMQuaternionMultiply(
			                  originalRotation, deltaRotation)));
		}

		return edited;
	};

	// 選択された祖先の子を二重に動かさず、全件の検証後にローカル変形を反映する。
	Scene working;
	working.ReplaceObjects(scene_.GetObjects());
	XMFLOAT4X4 beforeMatrix;
	XMStoreFloat4x4(&beforeMatrix,
	                TransformUtils::MakeWorldMatrix(beforePivot));
	XMFLOAT4X4 inverse;
	if (!TransformUtils::TryInverseWorldMatrix(beforeMatrix, inverse))
		return false;
	const auto delta =
	    XMLoadFloat4x4(&inverse) * TransformUtils::MakeWorldMatrix(afterPivot);
	for (const auto &snapshot : *pendingTransforms_) {
		auto *object = working.FindObject(snapshot.objectId);
		if (!object || object->parentId != snapshot.parentId)
			return false;
		object->transform = snapshot.before;

		if (snapshot.parentId == 0) {
			object->transform = makeEdited(snapshot);
			if (!TransformUtils::IsValid(object->transform))
				return false;
		} else if (!rotated && !scaled) {
			const auto original = snapshot.world.GetPosition();
			const XMFLOAT3 position{original.x + (afterPivot.translation.x -
			                                      beforePivot.translation.x),
			                        original.y + (afterPivot.translation.y -
			                                      beforePivot.translation.y),
			                        original.z + (afterPivot.translation.z -
			                                      beforePivot.translation.z)};
			if (!SceneHierarchy::SetWorldPosition(working, snapshot.objectId,
			                                      position))
				return false;
		} else {
			XMFLOAT4X4 matrix;
			XMStoreFloat4x4(&matrix,
			                XMLoadFloat4x4(&snapshot.world.matrix) * delta);
			if (!SceneHierarchy::SetWorldTransform(working, snapshot.objectId,
			                                       matrix))
				return false;
		}
	}

	SceneHierarchy::WorldTransforms transforms;
	if (!SceneHierarchy::BuildWorldTransforms(working, transforms))
		return false;
	for (const auto &snapshot : *pendingTransforms_) {
		scene_.FindObject(snapshot.objectId)->transform =
		    working.FindObject(snapshot.objectId)->transform;
	}

	return true;
}

bool EditorHistory::ResetTransforms(std::span<const SceneObjectId> objectIds) {
	CommitEdit();

	if (objectIds.empty()) {
		return false;
	}

	std::set<SceneObjectId> seen;
	std::vector<CommandGroup::Command> commands;
	commands.reserve(objectIds.size());
	const Transform reset;

	for (const SceneObjectId id : objectIds) {
		const SceneObject *object = scene_.FindObject(id);

		if (id == 0 || !seen.insert(id).second || !object ||
		    !TransformUtils::IsValid(object->transform)) {
			return false;
		}

		if (!TransformUtils::AreEqual(object->transform, reset)) {
			commands.emplace_back(
			    TransformCommand{id, object->transform, reset});
		}
	}

	if (commands.empty()) {
		return true;
	}

	CommandGroup group{std::move(commands)};
	const std::uint64_t next = PrepareAppend();

	if (!group.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(group)}, next,
	               objectIds.size() == 1 ? "Reset Transform"
	                                     : "Reset Transforms");
	return true;
}

bool EditorHistory::SetCollider(SceneObjectId objectId,
                                std::optional<Collider> collider) {
	CommitEdit();
	const auto *object = scene_.FindObject(objectId);

	if (!object)
		return false;

	AxisAlignedBox bounds;

	if (collider &&
	    !ColliderUtils::GetWorldBounds(*collider, scene_, objectId, bounds))
		return false;

	const bool adding = !object->collider;
	ColliderCommand command{objectId, object->collider, collider};

	if (!command.HasChanges())
		return true;

	const auto next = PrepareAppend();

	if (!command.Redo(scene_))
		return false;

	AppendPrepared(Command{std::move(command)}, next,
	               !collider ? "Remove Collider"
	               : adding  ? "Add Collider"
	                         : "Edit Collider");
	return true;
}

bool EditorHistory::SetColliders(std::span<const ColliderEdit> edits) {
	CommitEdit();

	if (edits.empty()) {
		return false;
	}

	std::set<SceneObjectId> seen;
	std::vector<CommandGroup::Command> commands;
	commands.reserve(edits.size());
	bool addingOnly = true;
	bool removingOnly = true;

	for (const auto &edit : edits) {
		const auto *object = scene_.FindObject(edit.objectId);

		if (!object || !seen.insert(edit.objectId).second ||
		    (object->collider && !ColliderUtils::IsValid(*object->collider))) {
			return false;
		}

		AxisAlignedBox bounds;

		if (edit.collider &&
		    !ColliderUtils::GetWorldBounds(*edit.collider, scene_,
		                                   edit.objectId, bounds)) {
			return false;
		}

		ColliderCommand command{edit.objectId, object->collider, edit.collider};

		if (command.HasChanges()) {
			addingOnly &= !object->collider.has_value();
			removingOnly &= !edit.collider.has_value();
			commands.emplace_back(std::move(command));
		}
	}

	if (commands.empty()) {
		return true;
	}

	CommandGroup group{std::move(commands)};
	const auto next = PrepareAppend();

	if (!group.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(group)}, next,
	               addingOnly     ? "Add Colliders"
	               : removingOnly ? "Remove Colliders"
	                              : "Edit Colliders");
	return true;
}

bool EditorHistory::BeginCollider(SceneObjectId objectId) {
	CommitEdit();
	const auto *object = scene_.FindObject(objectId);

	if (!object || !object->collider)
		return false;

	pending_ = PendingEdit{objectId, object->collider};
	return true;
}

bool EditorHistory::SetModelPath(SceneObjectId objectId,
                                 std::filesystem::path modelPath) {
	const auto *object = scene_.FindObject(objectId);
	const bool preload = object && object->mesh && object->mesh->preload;
	return SetMesh(objectId, MeshComponent{std::move(modelPath), preload});
}

bool EditorHistory::SetModelPaths(std::span<const SceneObjectId> ids,
                                  const std::filesystem::path &modelPath) {
	CommitEdit();
	const MeshComponent replacement{modelPath};

	if (ids.empty() || modelPath.empty() || !replacement.IsValid()) {
		return false;
	}

	std::set<SceneObjectId> seen;
	std::vector<CommandGroup::Command> commands;
	commands.reserve(ids.size());

	for (const auto id : ids) {
		const auto *object = scene_.FindObject(id);

		if (!seen.insert(id).second || !object || !object->mesh ||
		    !object->mesh->IsValid()) {
			return false;
		}

		auto next = replacement;
		next.preload = object->mesh->preload;
		if (*object->mesh != next) {
			commands.emplace_back(
			    ModelPathCommand{id, object->mesh, std::move(next)});
		}
	}

	if (commands.empty()) {
		return true;
	}

	CommandGroup group{std::move(commands)};
	const auto next = PrepareAppend();

	if (!group.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(group)}, next, "Replace Meshes");
	return true;
}

bool EditorHistory::SetMesh(SceneObjectId id,
                            std::optional<MeshComponent> value) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	const bool adding = !object->mesh;
	ModelPathCommand command{id, object->mesh, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Mesh"
	               : adding ? "Add Mesh"
	                        : "Change Mesh");
	return true;
}

bool EditorHistory::SetMeshRenderer(
    SceneObjectId id, std::optional<MeshRendererComponent> value) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	const bool adding = !object->meshRenderer;
	MeshRendererCommand command{id, object->meshRenderer, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Mesh Renderer"
	               : adding ? "Add Mesh Renderer"
	                        : "Edit Mesh Renderer");
	return true;
}

bool EditorHistory::SetVisibility(std::span<const SceneObjectId> objectIds,
                                  bool visible) {
	CommitEdit();

	if (objectIds.empty()) {
		return false;
	}

	std::set<SceneObjectId> seen;
	std::vector<CommandGroup::Command> commands;
	commands.reserve(objectIds.size());

	for (const auto id : objectIds) {
		const auto *object = scene_.FindObject(id);

		if (!object || !seen.insert(id).second) {
			return false;
		}

		if (object->meshRenderer && object->meshRenderer->visible != visible) {
			commands.emplace_back(
			    VisibilityCommand{id, object->meshRenderer->visible, visible});
		}
	}

	if (commands.empty()) {
		return true;
	}

	CommandGroup group{std::move(commands)};
	const auto next = PrepareAppend();

	if (!group.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(group)}, next,
	               visible ? "Show Models" : "Hide Models");
	return true;
}

bool EditorHistory::SetActive(std::span<const SceneObjectId> objectIds,
                              bool active) {
	CommitEdit();

	if (objectIds.empty()) {
		return false;
	}

	std::set<SceneObjectId> seen;
	std::vector<CommandGroup::Command> commands;
	commands.reserve(objectIds.size());

	for (const auto id : objectIds) {
		const auto *object = scene_.FindObject(id);

		if (!object || !seen.insert(id).second) {
			return false;
		}

		if (object->active != active) {
			commands.emplace_back(ActiveCommand{id, object->active, active});
		}
	}

	if (commands.empty()) {
		return true;
	}

	CommandGroup group{std::move(commands)};
	const auto next = PrepareAppend();

	if (!group.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(group)}, next,
	               active ? "Enable Objects" : "Disable Objects");
	return true;
}

bool EditorHistory::AlignCameraView(SceneObjectId id, const CameraPose &pose) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->camera || !object->camera->IsValid() ||
	    !TransformUtils::IsValid(object->transform)) {
		return false;
	}

	Scene working;

	if (!SceneCameraUtils::ResolveInitialDependencies(scene_, id, working)) {
		return false;
	}

	Transform desiredView, currentView;
	SceneWorldTransform currentWorld;

	if (!SceneCameraUtils::ApplyPose(pose, desiredView) ||
	    !SceneHierarchy::GetWorldTransform(working, id, currentWorld) ||
	    !SceneCameraUtils::ApplyPose(
	        SceneCameraUtils::FromWorldTransform(currentWorld), currentView)) {
		return false;
	}

	auto after = object->transform;
	auto follow = object->followCamera;
	// 同じ開始ビューでは、親の逆変換やオフセットの再計算による丸め誤差を履歴へ加えない。
	if (!TransformUtils::AreEqual(currentView, desiredView)) {
		auto desiredPose = pose;

		if (follow) {
			SceneWorldTransform target;
			if (object->character ||
			    !SceneHierarchy::GetWorldTransform(working, follow->target,
			                                       target) ||
			    !SceneCameraUtils::MatchFollowPose(pose, target.GetPosition(),
			                                       *follow)) {
				return false;
			}

			desiredPose =
			    SceneCameraUtils::MakeFollowPose(target.GetPosition(), *follow);
		}

		if (!SceneCameraUtils::ApplyWorldPose(working, id, desiredPose)) {
			return false;
		}

		after = working.FindObject(id)->transform;
	}

	std::vector<CommandGroup::Command> commands;
	commands.reserve(2);

	if (!TransformUtils::AreEqual(object->transform, after)) {
		commands.emplace_back(TransformCommand{id, object->transform, after});
	}

	if (object->followCamera != follow) {
		commands.emplace_back(
		    FollowCameraCommand{id, object->followCamera, follow});
	}

	if (commands.empty()) {
		return true;
	}

	CommandGroup group{std::move(commands)};
	const auto next = PrepareAppend();

	if (!group.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(group)}, next,
	               "Align Camera to Scene View");
	return true;
}

bool EditorHistory::SetTransforms(std::span<const SceneObjectId> objectIds,
                                  const Transform &transform) {
	CommitEdit();
	Transform after = transform;

	if (objectIds.empty() || !TransformUtils::IsValid(after) ||
	    !TransformUtils::TryNormalizeRotation(after.rotation, after.rotation)) {
		return false;
	}

	std::set<SceneObjectId> seen;
	std::vector<CommandGroup::Command> commands;
	commands.reserve(objectIds.size());

	for (const auto id : objectIds) {
		const auto *object = scene_.FindObject(id);

		if (!object || !seen.insert(id).second ||
		    !TransformUtils::IsValid(object->transform)) {
			return false;
		}

		if (!TransformUtils::AreEqual(object->transform, after)) {
			commands.emplace_back(
			    TransformCommand{id, object->transform, after});
		}
	}

	if (commands.empty()) {
		return true;
	}

	CommandGroup group{std::move(commands)};
	const auto next = PrepareAppend();

	if (!group.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(group)}, next,
	               objectIds.size() == 1 ? "Transform Object"
	                                     : "Transform Objects");
	return true;
}

bool EditorHistory::SetTransforms(std::span<const TransformEdit> edits) {
	CommitEdit();

	if (edits.empty()) {
		return false;
	}

	std::set<SceneObjectId> seen;
	std::vector<CommandGroup::Command> commands;
	commands.reserve(edits.size());

	for (const auto &edit : edits) {
		const auto *object = scene_.FindObject(edit.objectId);
		auto after = edit.transform;

		if (!object || !seen.insert(edit.objectId).second ||
		    !TransformUtils::IsValid(object->transform) ||
		    !TransformUtils::IsValid(after) ||
		    !TransformUtils::TryNormalizeRotation(after.rotation,
		                                          after.rotation)) {
			return false;
		}

		if (!TransformUtils::AreEqual(object->transform, after)) {
			commands.emplace_back(
			    TransformCommand{edit.objectId, object->transform, after});
		}
	}

	if (commands.empty()) {
		return true;
	}

	CommandGroup group{std::move(commands)};
	const auto state = PrepareAppend();

	if (!group.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(group)}, state,
	               edits.size() == 1 ? "Transform Object"
	                                 : "Transform Objects");
	return true;
}

bool EditorHistory::SnapObjectsToGrid(std::span<const SceneObjectId> objectIds,
                                      const DirectX::XMFLOAT3 &step) {
	CommitEdit();
	std::map<SceneObjectId, DirectX::XMFLOAT3> positions;

	if (!ReadWorldPositions(scene_, objectIds, positions)) {
		return false;
	}

	for (auto &[id, position] : positions) {
		(void)id;

		if (!MathUtils::SnapToGrid(position, step, position)) {
			return false;
		}
	}

	return ApplyWorldPositions(positions, "Snap Positions to Grid");
}

bool EditorHistory::TranslateObjects(std::span<const SceneObjectId> objectIds,
                                     const DirectX::XMFLOAT3 &translation) {
	CommitEdit();
	std::vector<SceneObjectId> roots;
	std::map<SceneObjectId, DirectX::XMFLOAT3> positions;
	if (!MathUtils::IsFinite(translation) ||
	    !SceneHierarchy::GetSelectionRoots(scene_, objectIds, roots) ||
	    !ReadWorldPositions(scene_, roots, positions)) {
		return false;
	}

	for (auto &[id, position] : positions) {
		(void)id;
		position = {position.x + translation.x, position.y + translation.y,
		            position.z + translation.z};
	}

	return ApplyWorldPositions(positions, roots.size() == 1 ? "Move Object"
	                                                        : "Move Objects");
}

bool EditorHistory::RenameObjects(std::span<const ObjectRename> renames) {
	CommitEdit();

	if (renames.empty()) {
		return false;
	}

	std::set<SceneObjectId> seen;
	std::vector<CommandGroup::Command> commands;
	commands.reserve(renames.size());

	for (const auto &rename : renames) {
		const auto *object = scene_.FindObject(rename.objectId);

		if (!object || !seen.insert(rename.objectId).second ||
		    rename.after.empty() || object->name != rename.before) {
			return false;
		}

		if (rename.before != rename.after) {
			commands.emplace_back(
			    RenameCommand{rename.objectId, rename.before, rename.after});
		}
	}

	if (commands.empty()) {
		return true;
	}

	CommandGroup group{std::move(commands)};
	const auto next = PrepareAppend();

	if (!group.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(group)}, next,
	               renames.size() == 1 ? "Rename Object" : "Rename Objects");
	return true;
}

bool EditorHistory::AlignObjects(std::span<const SceneObjectId> objectIds,
                                 SceneObjectId referenceId,
                                 AlignmentAxis axis) {
	CommitEdit();
	std::map<SceneObjectId, DirectX::XMFLOAT3> positions;
	if (!IsPlacementAxis(axis) ||
	    !ReadWorldPositions(scene_, objectIds, positions) ||
	    !positions.contains(referenceId)) {
		return false;
	}

	const float reference = Coordinate(positions.at(referenceId), axis);

	for (auto &[id, position] : positions) {
		(void)id;
		Coordinate(position, axis) = reference;
	}

	return ApplyWorldPositions(
	    positions, axis == AlignmentAxis::X   ? "Align X to Active"
	               : axis == AlignmentAxis::Y ? "Align Y to Active"
	                                          : "Align Z to Active");
}

bool EditorHistory::DistributeObjects(std::span<const SceneObjectId> objectIds,
                                      AlignmentAxis axis) {
	CommitEdit();
	std::map<SceneObjectId, DirectX::XMFLOAT3> positions;

	if (objectIds.size() < 3 || !IsPlacementAxis(axis) ||
	    !ReadWorldPositions(scene_, objectIds, positions)) {
		return false;
	}

	std::vector<SceneObjectId> order(objectIds.begin(), objectIds.end());
	std::sort(order.begin(), order.end(),
	          [&](SceneObjectId a, SceneObjectId b) {
		          const auto first = Coordinate(positions.at(a), axis),
		                     second = Coordinate(positions.at(b), axis);
		          return first != second ? first < second : a < b;
	          });
	const double first = Coordinate(positions.at(order.front()), axis);
	const double last = Coordinate(positions.at(order.back()), axis);

	for (std::size_t i = 1; i + 1 < order.size(); ++i) {
		const double ratio = double(i) / double(order.size() - 1);
		Coordinate(positions.at(order[i]), axis) =
		    static_cast<float>(std::lerp(first, last, ratio));
	}

	return ApplyWorldPositions(positions,
	                           axis == AlignmentAxis::X   ? "Distribute X"
	                           : axis == AlignmentAxis::Y ? "Distribute Y"
	                                                      : "Distribute Z");
}

bool EditorHistory::HasPendingChanges() const {
	if (pendingTransforms_) {
		for (const auto &snapshot : *pendingTransforms_) {
			const SceneObject *object = scene_.FindObject(snapshot.objectId);

			if (!object ||
			    !TransformUtils::AreEqual(snapshot.before, object->transform)) {
				return true;
			}
		}

		return false;
	}

	if (!pending_) {
		return false;
	}

	const SceneObject *object = scene_.FindObject(pending_->objectId);

	if (!object) {
		return true;
	}

	return std::visit(
	    [object](const auto &before) {
		    using Before = std::decay_t<decltype(before)>;

		    if constexpr (std::is_same_v<Before, Transform>) {
			    return !TransformUtils::AreEqual(before, object->transform);
		    } else if constexpr (std::is_same_v<Before,
		                                        std::optional<Collider>>) {
			    return before != object->collider;
		    } else if constexpr (std::is_same_v<
		                             Before, std::optional<CameraComponent>>) {
			    return before != object->camera;
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<AudioSourceComponent>>) {
			    return before != object->audioSource;
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<PointLightComponent>>) {
			    return before != object->pointLight;
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<SpotLightComponent>>) {
			    return before != object->spotLight;
		    } else if constexpr (std::is_same_v<
		                             Before, std::optional<ButtonComponent>>) {
			    return before != object->button;
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<RigidBodySettings>>) {
			    return before != object->rigidBody;
		    } else if constexpr (std::is_same_v<
		                             Before, std::optional<CanvasComponent>>) {
			    return before != object->canvas;
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<RectTransformComponent>>) {
			    return before != object->rectTransform;
		    } else if constexpr (std::is_same_v<Before,
		                                        std::optional<TextComponent>>) {
			    return before != object->text;
		    } else if constexpr (std::is_same_v<
		                             Before, std::optional<ImageComponent>>) {
			    return before != object->image;
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<MotionPathComponent>>) {
			    return before != object->motionPath;
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<
		                                 TransformAnimationComponent>>) {
			    return before != object->transformAnimation;
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<ParticleEmitterComponent>>) {
			    return before != object->particleEmitter;
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<AnimatorComponent>>) {
			    return before != object->animator;
		    } else if constexpr (std::is_same_v<Before,
		                                        std::optional<ColorTint>>) {
			    return before != object->colorTint;
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<MeshRendererComponent>>) {
			    return before != object->meshRenderer;
		    } else if constexpr (std::is_same_v<
		                             Before, std::optional<RespawnSettings>>) {
			    return before != object->respawn;
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<FollowCameraSettings>>) {
			    return before != object->followCamera;
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<CharacterSettings>>) {
			    return before != object->character;
		    } else {
			    return before != object->name;
		    }
	    },
	    pending_->before);
}

SceneObjectId EditorHistory::CreateObjectWithHistory(SceneObject object) {
	if (object.name.empty()) {
		object.name = "Object";
	}

	// シーン変更後の履歴記録で、メモリ確保を発生させない
	const std::uint64_t next = PrepareAppend();

	// 履歴用のデータを保持したまま、完成した状態の物体を作成する
	const SceneObjectId id = scene_.CreateObject(object);

	AppendPrepared(Command{ObjectCommand{
	                   id,
	                   std::move(object),
	                   ObjectCommand::Kind::CREATE,
	               }},
	               next, "Create Object");

	return id;
}

bool EditorHistory::BeginTransform(SceneObjectId objectId,
                                   const Transform &before) {
	if (!scene_.Contains(objectId) || !TransformUtils::IsValid(before)) {
		return false;
	}

	PendingEdit edit{
	    objectId,
	    before,
	};

	CommitEdit();
	pending_ = std::move(edit);

	return true;
}

void EditorHistory::CommitEdit() {
	if (pendingTransforms_) {
		std::vector<CommandGroup::Command> commands;
		commands.reserve(pendingTransforms_->size());

		for (const auto &snapshot : *pendingTransforms_) {
			const SceneObject *object = scene_.FindObject(snapshot.objectId);

			if (!object || !TransformUtils::IsValid(object->transform)) {
				Invalidate();
				return;
			}

			if (TransformUtils::AreEqual(snapshot.before, object->transform)) {
				continue;
			}

			commands.emplace_back(TransformCommand{
			    snapshot.objectId, snapshot.before, object->transform});
		}

		// 元の位置へ戻った場合は履歴へ追加しない
		if (commands.empty()) {
			pendingTransforms_.reset();
			return;
		}

		Command command{CommandGroup{std::move(commands)}};
		const std::uint64_t next = PrepareAppend();
		AppendPrepared(std::move(command), next,
		               pendingTransforms_->size() == 1 ? "Transform Object"
		                                               : "Transform Objects");
		pendingTransforms_.reset();
		return;
	}

	if (!pending_) {
		return;
	}

	SceneObject *object = scene_.FindObject(pending_->objectId);

	if (!object) {
		Invalidate();
		return;
	}

	if (std::holds_alternative<Transform>(pending_->before) &&
	    !TransformUtils::IsValid(object->transform)) {
		Invalidate();
		return;
	}

	if (std::holds_alternative<std::optional<Collider>>(pending_->before) &&
	    object->collider) {
		AxisAlignedBox bounds;

		if (!ColliderUtils::GetWorldBounds(*object->collider, scene_,
		                                   pending_->objectId, bounds)) {
			Invalidate();
			return;
		}
	}

	if (std::holds_alternative<std::optional<CharacterSettings>>(
	        pending_->before) &&
	    object->character && !object->character->IsValid()) {
		Invalidate();
		return;
	}

	if (std::holds_alternative<std::optional<FollowCameraSettings>>(
	        pending_->before) &&
	    object->followCamera && !object->followCamera->IsValid()) {
		Invalidate();
		return;
	}

	if (std::holds_alternative<std::optional<RespawnSettings>>(
	        pending_->before) &&
	    object->respawn && !object->respawn->IsValid()) {
		Invalidate();
		return;
	}

	if (std::holds_alternative<std::optional<AudioSourceComponent>>(
	        pending_->before) &&
	    object->audioSource && !object->audioSource->IsValid()) {
		Invalidate();
		return;
	}

	if (std::holds_alternative<std::optional<PointLightComponent>>(
	        pending_->before) &&
	    object->pointLight && !object->pointLight->IsValid()) {
		Invalidate();
		return;
	}

	if (std::holds_alternative<std::optional<SpotLightComponent>>(
	        pending_->before) &&
	    object->spotLight && !object->spotLight->IsValid()) {
		Invalidate();
		return;
	}

	if (std::holds_alternative<std::optional<ButtonComponent>>(
	        pending_->before) &&
	    object->button && !object->button->IsValid()) {
		Invalidate();
		return;
	}
	if (std::holds_alternative<std::optional<RigidBodySettings>>(
	        pending_->before) &&
	    object->rigidBody && !object->rigidBody->IsValid()) {
		Invalidate();
		return;
	}
	if (std::holds_alternative<std::optional<CanvasComponent>>(
	        pending_->before) &&
	    object->canvas && !object->canvas->IsValid()) {
		Invalidate();
		return;
	}
	if (std::holds_alternative<std::optional<RectTransformComponent>>(
	        pending_->before) &&
	    object->rectTransform && !object->rectTransform->IsValid()) {
		Invalidate();
		return;
	}
	if (std::holds_alternative<std::optional<TextComponent>>(
	        pending_->before) &&
	    object->text && !object->text->IsValid()) {
		Invalidate();
		return;
	}
	if (std::holds_alternative<std::optional<ImageComponent>>(
	        pending_->before) &&
	    object->image && !object->image->IsValid()) {
		Invalidate();
		return;
	}
	if (std::holds_alternative<std::optional<MotionPathComponent>>(
	        pending_->before) &&
	    object->motionPath && !object->motionPath->IsValid()) {
		Invalidate();
		return;
	}

	if (std::holds_alternative<std::optional<TransformAnimationComponent>>(
	        pending_->before) &&
	    object->transformAnimation && !object->transformAnimation->IsValid()) {
		Invalidate();
		return;
	}

	if (std::holds_alternative<std::optional<ParticleEmitterComponent>>(
	        pending_->before) &&
	    object->particleEmitter && !object->particleEmitter->IsValid()) {
		Invalidate();
		return;
	}

	if (std::holds_alternative<std::optional<AnimatorComponent>>(
	        pending_->before) &&
	    object->animator && !object->animator->IsValid()) {
		Invalidate();
		return;
	}

	if (std::holds_alternative<std::optional<ColorTint>>(pending_->before) &&
	    object->colorTint && !object->colorTint->IsValid()) {
		Invalidate();
		return;
	}

	if (std::holds_alternative<std::optional<MeshRendererComponent>>(
	        pending_->before) &&
	    object->meshRenderer && !object->meshRenderer->IsValid()) {
		Invalidate();
		return;
	}

	if (std::holds_alternative<std::optional<CameraComponent>>(
	        pending_->before) &&
	    object->camera && !object->camera->IsValid()) {
		Invalidate();
		return;
	}

	// 空の名前で確定した場合は、編集開始時点の名前へ戻す
	if (std::holds_alternative<std::string>(pending_->before) &&
	    object->name.empty()) {
		auto &before = std::get<std::string>(pending_->before);

		object->name.swap(before);
		pending_.reset();
		return;
	}

	if (!HasPendingChanges()) {
		pending_.reset();
		return;
	}

	const SceneObjectId objectId = pending_->objectId;

	// 記録内容の作成に成功してから、履歴へ追加する
	Command command = std::visit(
	    [objectId, object](const auto &before) -> Command {
		    using Before = std::decay_t<decltype(before)>;

		    if constexpr (std::is_same_v<Before, Transform>) {
			    return Command{TransformCommand{
			        objectId,
			        before,
			        object->transform,
			    }};
		    } else if constexpr (std::is_same_v<Before,
		                                        std::optional<Collider>>) {
			    return Command{
			        ColliderCommand{objectId, before, object->collider}};
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<AudioSourceComponent>>) {
			    return Command{
			        AudioSourceCommand{objectId, before, object->audioSource}};
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<PointLightComponent>>) {
			    return Command{
			        PointLightCommand{objectId, before, object->pointLight}};
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<SpotLightComponent>>) {
			    return Command{
			        SpotLightCommand{objectId, before, object->spotLight}};
		    } else if constexpr (std::is_same_v<
		                             Before, std::optional<ButtonComponent>>) {
			    return Command{ButtonCommand{objectId, before, object->button}};
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<RigidBodySettings>>) {
			    return Command{
			        RigidBodyCommand{objectId, before, object->rigidBody}};
		    } else if constexpr (std::is_same_v<
		                             Before, std::optional<CanvasComponent>>) {
			    return Command{CanvasCommand{objectId, before, object->canvas}};
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<RectTransformComponent>>) {
			    return Command{RectTransformCommand{objectId, before,
			                                        object->rectTransform}};
		    } else if constexpr (std::is_same_v<Before,
		                                        std::optional<TextComponent>>) {
			    return Command{TextCommand{objectId, before, object->text}};
		    } else if constexpr (std::is_same_v<
		                             Before, std::optional<ImageComponent>>) {
			    return Command{ImageCommand{objectId, before, object->image}};
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<MotionPathComponent>>) {
			    return Command{
			        MotionPathCommand{objectId, before, object->motionPath}};
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<
		                                 TransformAnimationComponent>>) {
			    return Command{TransformAnimationCommand{
			        objectId, before, object->transformAnimation}};
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<ParticleEmitterComponent>>) {
			    return Command{ParticleEmitterCommand{objectId, before,
			                                          object->particleEmitter}};
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<AnimatorComponent>>) {
			    return Command{
			        AnimatorCommand{objectId, before, object->animator}};
		    } else if constexpr (std::is_same_v<Before,
		                                        std::optional<ColorTint>>) {
			    return Command{
			        ColorTintCommand{objectId, before, object->colorTint}};
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<MeshRendererComponent>>) {
			    return Command{MeshRendererCommand{objectId, before,
			                                       object->meshRenderer}};
		    } else if constexpr (std::is_same_v<
		                             Before, std::optional<RespawnSettings>>) {
			    return Command{
			        RespawnCommand{objectId, before, object->respawn}};
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<FollowCameraSettings>>) {
			    return Command{FollowCameraCommand{objectId, before,
			                                       object->followCamera}};
		    } else if constexpr (std::is_same_v<
		                             Before,
		                             std::optional<CharacterSettings>>) {
			    return Command{
			        CharacterCommand{objectId, before, object->character}};
		    } else if constexpr (std::is_same_v<
		                             Before, std::optional<CameraComponent>>) {
			    return Command{CameraCommand{objectId, before, object->camera}};
		    } else {
			    return Command{RenameCommand{
			        objectId,
			        before,
			        object->name,
			    }};
		    }
	    },
	    pending_->before);

	const std::uint64_t next = PrepareAppend();

	const auto description = std::visit(
	    [](const auto &before) -> std::string_view {
		    using T = std::decay_t<decltype(before)>;

		    if constexpr (std::is_same_v<T, Transform>) {
			    return "Transform Object";
		    } else if constexpr (std::is_same_v<T, std::string>) {
			    return "Rename Object";
		    } else if constexpr (std::is_same_v<T, std::optional<Collider>>) {
			    return "Edit Collider";
		    } else if constexpr (std::is_same_v<
		                             T, std::optional<CharacterSettings>>) {
			    return "Edit Character Controller";
		    } else if constexpr (std::is_same_v<
		                             T, std::optional<FollowCameraSettings>>) {
			    return "Edit Follow Camera";
		    } else if constexpr (std::is_same_v<
		                             T, std::optional<CameraComponent>>) {
			    return "Edit Camera";
		    } else if constexpr (std::is_same_v<
		                             T, std::optional<AudioSourceComponent>>) {
			    return "Edit Audio Source";
		    } else if constexpr (std::is_same_v<
		                             T, std::optional<PointLightComponent>>) {
			    return "Edit Point Light";
		    } else if constexpr (std::is_same_v<
		                             T, std::optional<SpotLightComponent>>) {
			    return "Edit Spot Light";
		    } else if constexpr (std::is_same_v<
		                             T, std::optional<ButtonComponent>>) {
			    return "Edit UI Button";
		    } else if constexpr (std::is_same_v<
		                             T, std::optional<RigidBodySettings>>) {
			    return "Edit Rigid Body";
		    } else if constexpr (std::is_same_v<
		                             T, std::optional<CanvasComponent>>) {
			    return "Edit Canvas";
		    } else if constexpr (std::is_same_v<
		                             T,
		                             std::optional<RectTransformComponent>>) {
			    return "Edit Rect Transform";
		    } else if constexpr (std::is_same_v<T,
		                                        std::optional<TextComponent>>) {
			    return "Edit UI Text";
		    } else if constexpr (std::is_same_v<
		                             T, std::optional<ImageComponent>>) {
			    return "Edit UI Image";
		    } else if constexpr (std::is_same_v<
		                             T, std::optional<MotionPathComponent>>) {
			    return "Edit Motion Path";
		    } else if constexpr (std::is_same_v<
		                             T, std::optional<
		                                    TransformAnimationComponent>>) {
			    return "Edit Transform Animation";
		    } else if constexpr (std::is_same_v<
		                             T,
		                             std::optional<ParticleEmitterComponent>>) {
			    return "Edit Particle Emitter";
		    } else if constexpr (std::is_same_v<
		                             T, std::optional<AnimatorComponent>>) {
			    return "Edit Animator";
		    } else if constexpr (std::is_same_v<T, std::optional<ColorTint>>) {
			    return "Edit Color Tint";
		    } else if constexpr (std::is_same_v<
		                             T, std::optional<MeshRendererComponent>>) {
			    return "Edit Mesh Renderer";
		    } else {
			    return "Edit Respawn";
		    }
	    },
	    pending_->before);
	AppendPrepared(std::move(command), next, description);
	pending_.reset();
}

bool EditorHistory::IsEditing() const {
	return pending_.has_value() || pendingTransforms_.has_value();
}

bool EditorHistory::CanUndo() const {
	return !IsEditing() && cursor_ > 0;
}

bool EditorHistory::CanRedo() const {
	return !IsEditing() && cursor_ < entries_.size();
}

std::string_view EditorHistory::GetUndoDescription() const {
	return CanUndo() ? entries_[cursor_ - 1].description : std::string_view{};
}

std::string_view EditorHistory::GetRedoDescription() const {
	return CanRedo() ? entries_[cursor_].description : std::string_view{};
}

std::size_t EditorHistory::GetStateCount() const {
	return entries_.size() + 1;
}

std::optional<EditorHistory::State>
EditorHistory::GetState(std::size_t index) const {
	if (index > entries_.size()) {
		return std::nullopt;
	}

	const auto state =
	    index == 0
	        ? (entries_.empty() ? currentState_ : entries_.front().beforeState)
	        : entries_[index - 1].afterState;
	return State{index == 0 ? "History Start" : entries_[index - 1].description,
	             index == cursor_, state == savedState_};
}

bool EditorHistory::MoveTo(std::size_t index) {
	if (IsEditing() || index > entries_.size()) {
		return false;
	}

	if (index == cursor_) {
		return true;
	}

	try {
		const bool succeeded =
		    scene_.ApplyTransaction([this, index](Scene &working) {
			    if (index < cursor_) {
				    for (auto i = cursor_; i > index; --i) {
					    if (!std::visit(
					            [&working](const auto &command) {
						            return command.Undo(working);
					            },
					            entries_[i - 1].command)) {
						    return false;
					    }
				    }
			    } else {
				    for (auto i = cursor_; i < index; ++i) {
					    if (!std::visit(
					            [&working](const auto &command) {
						            return command.Redo(working);
					            },
					            entries_[i].command)) {
						    return false;
					    }
				    }
			    }

			    return true;
		    });

		if (!succeeded) {
			Invalidate();
			return false;
		}

		currentState_ = index == 0 ? entries_.front().beforeState
		                           : entries_[index - 1].afterState;
		cursor_ = index;
		return true;
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorHistory::MoveTo: {}", error.what()));
		return false;
	}
}

bool EditorHistory::Undo() {
	if (!CanUndo()) {
		return false;
	}

	try {
		const Entry &entry = entries_[cursor_ - 1];

		const bool succeeded = std::visit(
		    [this](const auto &command) { return command.Undo(scene_); },
		    entry.command);

		if (!succeeded) {
			Invalidate();
			return false;
		}

		currentState_ = entry.beforeState;
		--cursor_;

		return true;
	} catch (const std::exception &error) {
		// 復元時の確保失敗では、再試行できるよう履歴を保持する
		Logger::Error(std::format("EditorHistory::Undo: {}", error.what()));
		return false;
	}
}

bool EditorHistory::Redo() {
	if (!CanRedo()) {
		return false;
	}

	try {
		const Entry &entry = entries_[cursor_];

		const bool succeeded = std::visit(
		    [this](const auto &command) { return command.Redo(scene_); },
		    entry.command);

		if (!succeeded) {
			Invalidate();
			return false;
		}

		currentState_ = entry.afterState;
		++cursor_;

		return true;
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorHistory::Redo: {}", error.what()));
		return false;
	}
}

bool EditorHistory::IsModified() const {
	return currentState_ != savedState_ || HasPendingChanges();
}

void EditorHistory::MarkSaved() {
	CommitEdit();
	savedState_ = currentState_;
}

void EditorHistory::Invalidate() {
	pending_.reset();
	pendingTransforms_.reset();

	entries_.clear();
	cursor_ = 0;

	currentState_ = AllocateState();
}

std::unique_ptr<EditorHistory> EditorHistory::PrepareAssetRewrite(
    const SceneAssetReferences::ObjectRewrite &rewrite) const {
	if (IsEditing()) {
		throw std::runtime_error("Finish the current edit before moving assets.");
	}
	auto prepared = std::make_unique<EditorHistory>(scene_);
	prepared->entries_ = entries_;
	for (auto &entry : prepared->entries_) {
		std::visit([&](auto &command) {
			if constexpr (requires { command.RewriteObjects(rewrite); }) {
				command.RewriteObjects(rewrite);
			}
		}, entry.command);
	}
	return prepared;
}

void EditorHistory::ApplyAssetRewrite(EditorHistory &prepared) noexcept {
	entries_.swap(prepared.entries_);
}

void EditorHistory::VisitStoredObjects(
    const std::function<void(const SceneObject &)> &visit) const {
	const SceneAssetReferences::ObjectRewrite inspect = visit;
	for (const auto &entry : entries_) {
		std::visit([&](const auto &command) {
			using CommandType = std::decay_t<decltype(command)>;
			if constexpr (requires(CommandType &copy) { copy.RewriteObjects(inspect); }) {
				auto copy = command;
				copy.RewriteObjects(inspect);
			}
		}, entry.command);
	}
}

void EditorHistory::Reset() {
	pending_.reset();
	pendingTransforms_.reset();

	entries_.clear();
	cursor_ = 0;

	currentState_ = 0;
	savedState_ = 0;
	nextState_ = 1;
}

std::uint64_t EditorHistory::PrepareAppend() {
	// シーン変更後の記録では、メモリ確保を発生させない
	static_assert(std::is_nothrow_move_constructible_v<Entry>);

	if (cursor_ == entries_.capacity()) {
		const std::size_t capacity = entries_.capacity();
		const std::size_t maximum = entries_.max_size();

		if (capacity == maximum) {
			throw std::length_error(
			    "EditorHistory: 履歴件数の上限に達しました。");
		}

		const std::size_t nextCapacity =
		    capacity == 0 ? 1
		                  : (capacity > maximum / 2 ? maximum : capacity * 2);

		entries_.reserve(nextCapacity);
	}

	return AllocateState();
}

void EditorHistory::AppendPrepared(Command command, std::uint64_t nextState,
                                   std::string_view description) noexcept {
	assert(cursor_ < entries_.capacity());

	// Undo後の新しい編集では、Redo側の履歴を取り除く
	entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_),
	               entries_.end());

	entries_.push_back(Entry{
	    std::move(command),
	    currentState_,
	    nextState,
	    description,
	});

	++cursor_;
	currentState_ = nextState;
}

std::uint64_t EditorHistory::AllocateState() {
	if (nextState_ == (std::numeric_limits<std::uint64_t>::max)()) {
		throw std::overflow_error(
		    "EditorHistory: 状態番号の上限に達しました。");
	}

	return nextState_++;
}

bool EditorHistory::SetCharacter(SceneObjectId id,
                                 std::optional<CharacterSettings> value) {
	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	if (value && value->preferredInputTarget) {
		std::vector<CommandGroup::Command> commands;
		CharacterCommand target{id, object->character, value};
		if (target.HasChanges())
			commands.emplace_back(std::move(target));
		for (const auto &[otherId, other] : scene_.GetObjects()) {
			if (otherId == id || !other.character || !other.character->preferredInputTarget)
				continue;
			auto disabled = *other.character;
			disabled.preferredInputTarget = false;
			commands.emplace_back(CharacterCommand{otherId, other.character, disabled});
		}
		if (commands.empty())
			return true;
		CommandGroup group{std::move(commands)};
		const auto next = PrepareAppend();
		if (!group.Redo(scene_))
			return false;
		AppendPrepared(Command{std::move(group)}, next, "Set Preferred Player Character");
		return true;
	}

	const bool adding = !object->character;
	CharacterCommand command{id, object->character, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Character Controller"
	               : adding ? "Add Character Controller"
	                        : "Edit Character Controller");
	return true;
}

bool EditorHistory::BeginCharacter(SceneObjectId id) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->character) {
		return false;
	}

	pending_ = PendingEdit{id, object->character};
	return true;
}

bool EditorHistory::SetFollowCamera(SceneObjectId id,
                                    std::optional<FollowCameraSettings> value) {
	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object ||
	    (value && (!value->IsValid() || !SceneCameraUtils::CanSetFollowTarget(
	                                        scene_, id, value->target)))) {
		return false;
	}

	const bool adding = !object->followCamera;
	FollowCameraCommand command{id, object->followCamera, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Follow Camera"
	               : adding ? "Add Follow Camera"
	                        : "Edit Follow Camera");
	return true;
}

bool EditorHistory::BeginFollowCamera(SceneObjectId id) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->followCamera) {
		return false;
	}

	pending_ = PendingEdit{id, object->followCamera};
	return true;
}

bool EditorHistory::SetRespawn(SceneObjectId id,
                               std::optional<RespawnSettings> value) {
	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	const bool adding = !object->respawn;
	RespawnCommand command{id, object->respawn, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Respawn"
	               : adding ? "Add Respawn"
	                        : "Edit Respawn");
	return true;
}

bool EditorHistory::BeginRespawn(SceneObjectId id) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->respawn) {
		return false;
	}

	pending_ = PendingEdit{id, object->respawn};
	return true;
}

bool EditorHistory::SetColorTint(SceneObjectId id,
                                 std::optional<ColorTint> value) {
	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	const bool adding = !object->colorTint;
	ColorTintCommand command{id, object->colorTint, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Color Tint"
	               : adding ? "Add Color Tint"
	                        : "Edit Color Tint");
	return true;
}

bool EditorHistory::BeginMeshRenderer(SceneObjectId id) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->meshRenderer) {
		return false;
	}

	pending_ = PendingEdit{id, object->meshRenderer};
	return true;
}

bool EditorHistory::BeginColorTint(SceneObjectId id) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->colorTint) {
		return false;
	}

	pending_ = PendingEdit{id, object->colorTint};
	return true;
}

bool EditorHistory::SetCamera(SceneObjectId id,
                              std::optional<CameraComponent> value) {
	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	if (value && value->primary) {
		if (object->camera && !object->camera->IsValid()) {
			return false;
		}

		const bool adding = !object->camera;
		const bool activating = adding || !object->camera->primary;
		std::vector<CommandGroup::Command> commands;
		CameraCommand target{id, object->camera, value};

		if (target.HasChanges()) {
			commands.emplace_back(std::move(target));
		}

		bool clearedOther = false;

		for (const auto &[otherId, other] : scene_.GetObjects()) {
			if (otherId == id || !other.camera || !other.camera->primary) {
				continue;
			}

			if (!other.camera->IsValid()) {
				return false;
			}

			auto disabled = *other.camera;
			disabled.primary = false;
			commands.emplace_back(
			    CameraCommand{otherId, other.camera, disabled});
			clearedOther = true;
		}

		if (commands.empty()) {
			return true;
		}

		CommandGroup group{std::move(commands)};
		const auto next = PrepareAppend();

		if (!group.Redo(scene_)) {
			return false;
		}

		AppendPrepared(Command{std::move(group)}, next,
		               adding                       ? "Add Camera"
		               : activating || clearedOther ? "Set Primary Camera"
		                                            : "Edit Camera");
		return true;
	}

	const bool adding = !object->camera;
	CameraCommand command{id, object->camera, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Camera"
	               : adding ? "Add Camera"
	                        : "Edit Camera");
	return true;
}

bool EditorHistory::SetRespawnSettings(std::span<const SceneObjectId> ids,
                                       const RespawnSettings &settings) {
	CommitEdit();

	if (ids.empty() || !settings.IsValid()) {
		return false;
	}

	std::set<SceneObjectId> seen;
	std::vector<CommandGroup::Command> commands;
	commands.reserve(ids.size());

	for (const auto id : ids) {
		const auto *object = scene_.FindObject(id);

		if (!seen.insert(id).second || !object ||
		    (object->respawn && !object->respawn->IsValid())) {
			return false;
		}

		RespawnCommand command{id, object->respawn, settings};

		if (command.HasChanges()) {
			commands.emplace_back(std::move(command));
		}
	}

	if (commands.empty()) {
		return true;
	}

	CommandGroup group{std::move(commands)};
	const auto state = PrepareAppend();

	if (!group.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(group)}, state, "Paste Respawn Settings");
	return true;
}

bool EditorHistory::BeginCamera(SceneObjectId id) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->camera) {
		return false;
	}

	pending_ = PendingEdit{id, object->camera};
	return true;
}

bool EditorHistory::ResetComponentSettings(SceneObjectId id,
                                           ComponentSettings settings) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object) {
		return false;
	}

	const auto reset = [&](auto command, std::string_view description) {
		if (!command.HasChanges()) {
			return true;
		}

		const auto next = PrepareAppend();

		if (!command.Redo(scene_)) {
			return false;
		}

		AppendPrepared(Command{std::move(command)}, next, description);
		return true;
	};

	switch (settings) {
	case ComponentSettings::CHARACTER:
		if (!object->character || !object->character->IsValid()) {
			return false;
		}

		return reset(
		    CharacterCommand{id, object->character, CharacterSettings{}},
		    "Reset Character Settings");
	case ComponentSettings::FOLLOW_CAMERA: {
		if (!object->followCamera || !object->followCamera->IsValid()) {
			return false;
		}

		FollowCameraSettings defaults;
		// 参照を変更しないため、不在や循環が残っていても数値設定だけを戻せる。
		defaults.target = object->followCamera->target;
		return reset(FollowCameraCommand{id, object->followCamera, defaults},
		             "Reset Follow Settings");
	}
	case ComponentSettings::RESPAWN:
		if (!object->respawn || !object->respawn->IsValid()) {
			return false;
		}

		return reset(RespawnCommand{id, object->respawn, RespawnSettings{}},
		             "Reset Respawn Settings");
	case ComponentSettings::CAMERA_LENS: {
		if (!object->camera || !object->camera->IsValid()) {
			return false;
		}

		auto defaults = *object->camera;
		defaults.settings = CameraSettings{};
		return reset(CameraCommand{id, object->camera, defaults},
		             "Reset Lens Settings");
	}
	default:
		return false;
	}
}

bool EditorHistory::SetTexturePath(SceneObjectId id, std::uint32_t slot,
                                   const std::filesystem::path &path) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->meshRenderer || !object->meshRenderer->IsValid()) {
		return false;
	}

	auto next = object->meshRenderer->textures;

	std::erase_if(next.slots,
	              [slot](const auto &entry) { return entry.first == slot; });

	if (!path.empty()) {
		next.slots.emplace_back(slot, path);
		std::sort(
		    next.slots.begin(), next.slots.end(),
		    [](const auto &a, const auto &b) { return a.first < b.first; });
	}

	if (!next.IsValid()) {
		return false;
	}

	if (object->meshRenderer->textures == next) {
		return true;
	}

	TextureSlotsCommand command{id, object->meshRenderer->textures,
	                            std::move(next)};
	const auto state = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, state, "Assign Texture");
	return true;
}

bool EditorHistory::SetTextureUVs(std::span<const SceneObjectId> ids,
                                  const std::array<float, 2> &scale,
                                  const std::array<float, 2> &offset) {
	CommitEdit();
	MeshRendererComponent values;
	values.uvScale = scale;
	values.uvOffset = offset;

	if (ids.empty() || !values.IsValid()) {
		return false;
	}

	std::set<SceneObjectId> seen;
	std::vector<CommandGroup::Command> commands;
	commands.reserve(ids.size());

	for (const auto id : ids) {
		const auto *object = scene_.FindObject(id);

		if (!seen.insert(id).second || !object || !object->meshRenderer ||
		    !object->meshRenderer->IsValid()) {
			return false;
		}

		auto next = *object->meshRenderer;
		next.uvScale = values.uvScale;
		next.uvOffset = values.uvOffset;

		if (next != *object->meshRenderer) {
			commands.emplace_back(
			    MeshRendererCommand{id, object->meshRenderer, std::move(next)});
		}
	}

	if (commands.empty()) {
		return true;
	}

	CommandGroup group{std::move(commands)};
	const auto next = PrepareAppend();

	if (!group.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(group)}, next, "Apply UV to Selection");
	return true;
}

bool EditorHistory::SetTexturePaths(std::span<const SceneObjectId> ids,
                                    std::uint32_t slot,
                                    const std::filesystem::path &path) {
	CommitEdit();

	if (ids.empty()) {
		return false;
	}

	std::set<SceneObjectId> seen;
	std::vector<CommandGroup::Command> commands;
	commands.reserve(ids.size());

	for (const auto id : ids) {
		const auto *object = scene_.FindObject(id);

		if (!seen.insert(id).second || !object || !object->meshRenderer ||
		    !object->meshRenderer->IsValid()) {
			return false;
		}

		auto next = object->meshRenderer->textures;
		std::erase_if(next.slots, [slot](const auto &binding) {
			return binding.first == slot;
		});

		if (!path.empty()) {
			next.slots.emplace_back(slot, path);
			std::sort(
			    next.slots.begin(), next.slots.end(),
			    [](const auto &a, const auto &b) { return a.first < b.first; });
		}

		if (!next.IsValid()) {
			return false;
		}

		if (next != object->meshRenderer->textures) {
			commands.emplace_back(TextureSlotsCommand{
			    id, object->meshRenderer->textures, std::move(next)});
		}
	}

	if (commands.empty()) {
		return true;
	}

	CommandGroup group{std::move(commands)};
	const auto next = PrepareAppend();

	if (!group.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(group)}, next, "Assign Textures");
	return true;
}

bool EditorHistory::ApplyWorldPositions(
    const std::map<SceneObjectId, DirectX::XMFLOAT3> &positions,
    std::string_view description) {
	Scene working;
	working.ReplaceObjects(scene_.GetObjects());

	if (!SceneHierarchy::SetWorldPositions(working, positions)) {
		return false;
	}

	std::vector<CommandGroup::Command> commands;
	commands.reserve(positions.size());

	for (const auto &[id, position] : positions) {
		(void)position;
		const auto &before = scene_.FindObject(id)->transform;
		const auto &after = working.FindObject(id)->transform;

		if (!TransformUtils::AreEqual(before, after)) {
			commands.emplace_back(TransformCommand{id, before, after});
		}
	}

	if (commands.empty()) {
		return true;
	}

	CommandGroup group{std::move(commands)};
	const auto next = PrepareAppend();

	if (!group.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(group)}, next, description);
	return true;
}

bool EditorHistory::SetWorldPosition(SceneObjectId id,
                                     const DirectX::XMFLOAT3 &position) {
	CommitEdit();
	return ApplyWorldPositions({{id, position}}, "Move Object");
}

bool EditorHistory::SetTriggerActions(
    SceneObjectId id, std::optional<TriggerActionsComponent> value) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	if (value) {
		for (const auto &action : value->actions) {
			if (action.target != 0 && !scene_.Contains(action.target)) {
				return false;
			}
		}
	}

	const bool adding = !object->triggerActions;
	TriggerActionsCommand command{id, object->triggerActions, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Trigger Actions"
	               : adding ? "Add Trigger Actions"
	                        : "Edit Trigger Actions");
	return true;
}

bool EditorHistory::SceneLightingCommand::Undo(Scene &scene) const {
	return scene.SetLighting(before) && scene.SetFog(fogBefore);
}

bool EditorHistory::SceneLightingCommand::Redo(Scene &scene) const {
	return scene.SetLighting(after) && scene.SetFog(fogAfter);
}

bool EditorHistory::SetSceneLighting(std::optional<LightingSettings> lighting,
                                     const FogSettings &fog) {
	if ((lighting && !lighting->IsValid()) || !fog.IsValid()) {
		return false;
	}

	CommitEdit();

	if (scene_.GetLighting() == lighting && scene_.GetFog() == fog) {
		return true;
	}

	SceneLightingCommand command{scene_.GetLighting(), lighting, scene_.GetFog(), fog};
	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next, "Edit Scene Lighting");
	return true;
}

bool EditorHistory::SetPointLight(SceneObjectId id,
                                  std::optional<PointLightComponent> value) {
	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	const bool adding = !object->pointLight;
	PointLightCommand command{id, object->pointLight, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Point Light"
	               : adding ? "Add Point Light"
	                        : "Edit Point Light");
	return true;
}

bool EditorHistory::BeginPointLight(SceneObjectId id) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->pointLight) {
		return false;
	}

	pending_ = PendingEdit{id, object->pointLight};
	return true;
}

bool EditorHistory::SetAudioSource(SceneObjectId id,
                                   std::optional<AudioSourceComponent> value) {
	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	const bool adding = !object->audioSource;
	AudioSourceCommand command{id, object->audioSource, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Audio Source"
	               : adding ? "Add Audio Source"
	                        : "Edit Audio Source");
	return true;
}

bool EditorHistory::SetAudioListener(
    SceneObjectId id, std::optional<AudioListenerComponent> value) {
	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	if (value && value->preferred) {
		std::vector<CommandGroup::Command> commands;
		AudioListenerCommand target{id, object->audioListener, value};
		if (target.HasChanges())
			commands.emplace_back(std::move(target));
		for (const auto &[otherId, other] : scene_.GetObjects()) {
			if (otherId == id || !other.audioListener || !other.audioListener->preferred)
				continue;
			auto disabled = *other.audioListener;
			disabled.preferred = false;
			commands.emplace_back(AudioListenerCommand{otherId, other.audioListener, disabled});
		}
		if (commands.empty())
			return true;
		CommandGroup group{std::move(commands)};
		const auto next = PrepareAppend();
		if (!group.Redo(scene_))
			return false;
		AppendPrepared(Command{std::move(group)}, next, "Set Preferred Audio Listener");
		return true;
	}

	const bool adding = !object->audioListener;
	AudioListenerCommand command{id, object->audioListener, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Audio Listener"
	               : adding ? "Add Audio Listener"
	                        : "Edit Audio Listener");
	return true;
}

bool EditorHistory::BeginAudioSource(SceneObjectId id) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->audioSource) {
		return false;
	}

	pending_ = PendingEdit{id, object->audioSource};
	return true;
}

bool EditorHistory::SetSpotLight(SceneObjectId id,
                                 std::optional<SpotLightComponent> value) {
	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	const bool adding = !object->spotLight;
	SpotLightCommand command{id, object->spotLight, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Spot Light"
	               : adding ? "Add Spot Light"
	                        : "Edit Spot Light");
	return true;
}

bool EditorHistory::BeginSpotLight(SceneObjectId id) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->spotLight) {
		return false;
	}

	pending_ = PendingEdit{id, object->spotLight};
	return true;
}

bool EditorHistory::SetMotionPath(SceneObjectId id,
                                  std::optional<MotionPathComponent> value) {
	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	const bool adding = !object->motionPath;
	MotionPathCommand command{id, object->motionPath, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Motion Path"
	               : adding ? "Add Motion Path"
	                        : "Edit Motion Path");
	return true;
}

bool EditorHistory::BeginMotionPath(SceneObjectId id) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->motionPath) {
		return false;
	}

	pending_ = PendingEdit{id, object->motionPath};
	return true;
}

bool EditorHistory::SetTransformAnimation(
    SceneObjectId id, std::optional<TransformAnimationComponent> value) {
	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	const bool adding = !object->transformAnimation;
	TransformAnimationCommand command{id, object->transformAnimation, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Transform Animation"
	               : adding ? "Add Transform Animation"
	                        : "Edit Transform Animation");
	return true;
}

bool EditorHistory::BeginTransformAnimation(SceneObjectId id) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->transformAnimation) {
		return false;
	}

	pending_ = PendingEdit{id, object->transformAnimation};
	return true;
}

bool EditorHistory::SetParticleEmitter(
    SceneObjectId id, std::optional<ParticleEmitterComponent> value) {
	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	const bool adding = !object->particleEmitter;
	ParticleEmitterCommand command{id, object->particleEmitter, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Particle Emitter"
	               : adding ? "Add Particle Emitter"
	                        : "Edit Particle Emitter");
	return true;
}

bool EditorHistory::BeginParticleEmitter(SceneObjectId id) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->particleEmitter) {
		return false;
	}

	pending_ = PendingEdit{id, object->particleEmitter};
	return true;
}

bool EditorHistory::SetAnimator(SceneObjectId id,
                                std::optional<AnimatorComponent> value) {
	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	const bool adding = !object->animator;
	AnimatorCommand command{id, object->animator, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Animator"
	               : adding ? "Add Animator"
	                        : "Edit Animator");
	return true;
}

bool EditorHistory::BeginAnimator(SceneObjectId id) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->animator) {
		return false;
	}

	pending_ = PendingEdit{id, object->animator};
	return true;
}

bool EditorHistory::ApplySceneChanges(
    const std::function<bool(Scene &)> &operation,
    std::string_view description) {
	if (IsEditing() || !operation) {
		return false;
	}

	std::optional<ScenePatchCommand> command;
	std::optional<std::uint64_t> state;
	const bool applied = scene_.ApplyTransaction([&](Scene &candidate) {
		if (!operation(candidate)) {
			return false;
		}

		std::string text;

		if (!SceneSerializer::Serialize(candidate, text)) {
			return false;
		}

		command.emplace(scene_, candidate);

		if (command->HasChanges()) {
			state = PrepareAppend();
		}

		return true;
	});

	if (applied && state) {
		AppendPrepared(Command{std::move(*command)}, *state, description);
	}

	return applied;
}

bool EditorHistory::SetScript(SceneObjectId id,
                              std::optional<ScriptComponent> value) {
	if (value) {
		std::string normalized, error;
		if (!LuaScript::NormalizeState(value->initialState, normalized, error))
			return false;
		value->initialState = std::move(normalized);
	}

	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	const bool adding = !object->script;
	ScriptCommand command{id, object->script, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Lua Script"
	               : adding ? "Add Lua Script"
	                        : "Edit Lua Script");
	return true;
}

bool EditorHistory::SetCanvas(SceneObjectId id,
                              std::optional<CanvasComponent> value) {
	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	const bool adding = !object->canvas;
	CanvasCommand command{id, object->canvas, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Canvas"
	               : adding ? "Add Canvas"
	                        : "Edit Canvas");
	return true;
}

bool EditorHistory::BeginCanvas(SceneObjectId id) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->canvas) {
		return false;
	}

	pending_ = PendingEdit{id, object->canvas};
	return true;
}

bool EditorHistory::SetRectTransform(
    SceneObjectId id, std::optional<RectTransformComponent> value) {
	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	const bool adding = !object->rectTransform;
	RectTransformCommand command{id, object->rectTransform, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Rect Transform"
	               : adding ? "Add Rect Transform"
	                        : "Edit Rect Transform");
	return true;
}

bool EditorHistory::BeginRectTransform(SceneObjectId id) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->rectTransform) {
		return false;
	}

	pending_ = PendingEdit{id, object->rectTransform};
	return true;
}

bool EditorHistory::SetText(SceneObjectId id,
                            std::optional<TextComponent> value) {
	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	const bool adding = !object->text;
	TextCommand command{id, object->text, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove UI Text"
	               : adding ? "Add UI Text"
	                        : "Edit UI Text");
	return true;
}

bool EditorHistory::BeginText(SceneObjectId id) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->text) {
		return false;
	}

	pending_ = PendingEdit{id, object->text};
	return true;
}

bool EditorHistory::SetImage(SceneObjectId id,
                             std::optional<ImageComponent> value) {
	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	const bool adding = !object->image;
	ImageCommand command{id, object->image, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove UI Image"
	               : adding ? "Add UI Image"
	                        : "Edit UI Image");
	return true;
}

bool EditorHistory::BeginImage(SceneObjectId id) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->image) {
		return false;
	}

	pending_ = PendingEdit{id, object->image};
	return true;
}

bool EditorHistory::SetButton(SceneObjectId id,
                              std::optional<ButtonComponent> value) {
	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	const bool adding = !object->button;
	ButtonCommand command{id, object->button, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove UI Button"
	               : adding ? "Add UI Button"
	                        : "Edit UI Button");
	return true;
}

bool EditorHistory::BeginButton(SceneObjectId id) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->button) {
		return false;
	}

	pending_ = PendingEdit{id, object->button};
	return true;
}

bool EditorHistory::SetRigidBody(SceneObjectId id,
                                 std::optional<RigidBodySettings> value) {
	CommitEdit();
	auto *object = scene_.FindObject(id);

	if (!object || (value && !value->IsValid())) {
		return false;
	}

	const bool adding = !object->rigidBody;
	RigidBodyCommand command{id, object->rigidBody, value};

	if (!command.HasChanges()) {
		return true;
	}

	const auto next = PrepareAppend();

	if (!command.Redo(scene_)) {
		return false;
	}

	AppendPrepared(Command{std::move(command)}, next,
	               !value   ? "Remove Rigid Body"
	               : adding ? "Add Rigid Body"
	                        : "Edit Rigid Body");
	return true;
}

bool EditorHistory::BeginRigidBody(SceneObjectId id) {
	CommitEdit();
	const auto *object = scene_.FindObject(id);

	if (!object || !object->rigidBody) {
		return false;
	}

	pending_ = PendingEdit{id, object->rigidBody};
	return true;
}
