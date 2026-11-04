#include "scene/SceneHierarchy.h"

#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include "scene/Scene.h"

#include <cmath>
#include <set>

namespace {
/// <summary>
/// ローカルSRTを親のワールド行列と向きへ合成する
/// </summary>
/// <param name="local">有効なローカル変形</param>
/// <param name="parent">親の合成結果。nullptrはルート</param>
/// <param name="world">成功時だけ置き換える合成結果</param>
/// <returns>true:成功 false:変形を有限な可逆行列で表現できない</returns>
bool Compose(const Transform &local, const SceneWorldTransform *parent,
             SceneWorldTransform &world) {
	using namespace DirectX;

	if (!TransformUtils::IsValid(local)) {
		return false;
	}

	SceneWorldTransform result;
	auto matrix = TransformUtils::MakeWorldMatrix(local);
	result.rotation = local.rotation;

	if (parent) {
		matrix = matrix * XMLoadFloat4x4(&parent->matrix);
		const auto rotation =
		    XMMatrixRotationQuaternion(XMLoadFloat4(&local.rotation)) *
		    XMMatrixRotationQuaternion(XMLoadFloat4(&parent->rotation));
		XMStoreFloat4(
		    &result.rotation,
		    XMQuaternionNormalize(XMQuaternionRotationMatrix(rotation)));
	}

	XMStoreFloat4x4(&result.matrix, matrix);
	XMFLOAT4X4 inverse;

	if (!TransformUtils::TryInverseWorldMatrix(result.matrix, inverse)) {
		return false;
	}

	world = result;
	return true;
}

/// <summary>
/// 親のワールド変形を取得する。ルートの場合は単位変形を返す
/// </summary>
/// <param name="scene">親を取得するシーン</param>
/// <param name="parent">親ID。0はルート</param>
/// <param name="world">成功時だけ置き換える親の変形</param>
/// <returns>true:成功 false:親子関係や変形が無効</returns>
bool GetParentTransform(const Scene &scene, SceneObjectId parent,
                        SceneWorldTransform &world) {
	if (parent == 0) {
		world = {};
		return true;
	}

	return SceneHierarchy::GetWorldTransform(scene, parent, world);
}
} // namespace

bool SceneHierarchy::GetEvaluationOrder(const Scene &scene,
                                        std::vector<SceneObjectId> &order) {
	std::vector<SceneObjectId> result;
	result.reserve(scene.GetObjects().size());
	std::set<SceneObjectId> completed;

	for (const auto &[id, object] : scene.GetObjects()) {
		(void)object;
		std::vector<SceneObjectId> pending;
		std::set<SceneObjectId> visiting;
		auto current = id;

		while (current != 0 && !completed.contains(current)) {
			const auto *node = scene.FindObject(current);

			if (!node || !visiting.insert(current).second) {
				return false;
			}

			pending.push_back(current);
			current = node->parentId;
		}

		for (auto it = pending.rbegin(); it != pending.rend(); ++it) {
			completed.insert(*it);
			result.push_back(*it);
		}
	}

	order.swap(result);
	return true;
}

std::vector<SceneObjectId> SceneHierarchy::GetChildren(const Scene &scene,
                                                       SceneObjectId parent) {
	std::vector<SceneObjectId> result;

	for (const auto &[id, object] : scene.GetObjects()) {
		if (object.parentId == parent) {
			result.push_back(id);
		}
	}

	return result;
}

bool SceneHierarchy::GetSubtree(const Scene &scene, SceneObjectId id,
                                std::vector<SceneObjectId> &objects) {
	std::vector<SceneObjectId> order, result;
	if (!scene.Contains(id) || !GetEvaluationOrder(scene, order))
		return false;
	std::set<SceneObjectId> included;
	for (const auto current : order) {
		if (current == id ||
		    included.contains(scene.FindObject(current)->parentId)) {
			result.push_back(current);
			included.insert(current);
		}
	}
	objects.swap(result);
	return true;
}

bool SceneHierarchy::GetSelectionRoots(const Scene &scene,
                                       std::span<const SceneObjectId> ids,
                                       std::vector<SceneObjectId> &roots) {
	std::vector<SceneObjectId> order, result;
	if (ids.empty() || !GetEvaluationOrder(scene, order))
		return false;
	std::set<SceneObjectId> selected;
	for (const auto id : ids) {
		if (!scene.Contains(id) || !selected.insert(id).second)
			return false;
	}
	for (const auto id : ids) {
		auto parent = scene.FindObject(id)->parentId;
		while (parent != 0 && !selected.contains(parent))
			parent = scene.FindObject(parent)->parentId;
		if (parent == 0)
			result.push_back(id);
	}
	roots.swap(result);
	return true;
}

bool SceneHierarchy::ExpandSelection(const Scene &scene,
                                     std::span<const SceneObjectId> ids,
                                     std::vector<SceneObjectId> &objects) {
	std::vector<SceneObjectId> roots, order, result;
	if (!GetSelectionRoots(scene, ids, roots) ||
	    !GetEvaluationOrder(scene, order))
		return false;
	std::set<SceneObjectId> included(roots.begin(), roots.end());
	for (const auto id : order) {
		if (included.contains(id) ||
		    included.contains(scene.FindObject(id)->parentId)) {
			included.insert(id);
			result.push_back(id);
		}
	}
	objects.swap(result);
	return true;
}

bool SceneHierarchy::BuildWorldTransforms(const Scene &scene,
                                          WorldTransforms &transforms) {
	std::vector<SceneObjectId> order;

	if (!GetEvaluationOrder(scene, order)) {
		return false;
	}

	WorldTransforms result;

	for (const auto id : order) {
		const auto &object = *scene.FindObject(id);
		const auto *parent =
		    object.parentId == 0 ? nullptr : &result.at(object.parentId);
		SceneWorldTransform world;

		if (!Compose(object.transform, parent, world)) {
			return false;
		}

		result.emplace(id, world);
	}

	transforms.swap(result);
	return true;
}

bool SceneHierarchy::GetWorldTransform(const Scene &scene, SceneObjectId id,
                                       SceneWorldTransform &transform) {
	if (id == 0) {
		return false;
	}

	std::vector<SceneObjectId> pending;
	std::set<SceneObjectId> visiting;
	auto current = id;

	while (current != 0) {
		const auto *object = scene.FindObject(current);

		if (!object || !visiting.insert(current).second) {
			return false;
		}

		pending.push_back(current);
		current = object->parentId;
	}

	SceneWorldTransform result;

	for (auto it = pending.rbegin(); it != pending.rend(); ++it) {
		SceneWorldTransform next;

		if (!Compose(scene.FindObject(*it)->transform, &result, next)) {
			return false;
		}

		result = next;
	}

	transform = result;
	return true;
}

bool SceneHierarchy::SetParent(Scene &scene, SceneObjectId id,
                               SceneObjectId parent, bool keepWorld) {
	auto *object = scene.FindObject(id);
	std::vector<SceneObjectId> order;

	if (!object || id == parent || (parent != 0 && !scene.Contains(parent)) ||
	    !GetEvaluationOrder(scene, order)) {
		return false;
	}

	if (object->parentId == parent) {
		return true;
	}

	for (auto ancestor = parent; ancestor != 0;
	     ancestor = scene.FindObject(ancestor)->parentId) {
		if (ancestor == id) {
			return false;
		}
	}

	SceneWorldTransform parentWorld;

	if (!GetParentTransform(scene, parent, parentWorld)) {
		return false;
	}

	auto local = object->transform;

	if (keepWorld) {
		SceneWorldTransform world;
		DirectX::XMFLOAT4X4 inverse, relative;

		if (!GetWorldTransform(scene, id, world) ||
		    !TransformUtils::TryInverseWorldMatrix(parentWorld.matrix,
		                                           inverse)) {
			return false;
		}

		DirectX::XMStoreFloat4x4(&relative,
		                         DirectX::XMLoadFloat4x4(&world.matrix) *
		                             DirectX::XMLoadFloat4x4(&inverse));

		if (!TransformUtils::TryFromWorldMatrix(relative, local)) {
			return false;
		}
	}

	SceneWorldTransform candidate;

	if (!Compose(local, &parentWorld, candidate)) {
		return false;
	}

	object->transform = local;
	object->parentId = parent;
	return true;
}

bool SceneHierarchy::SetWorldTransform(Scene &scene, SceneObjectId id,
                                       const DirectX::XMFLOAT4X4 &matrix) {
	auto *object = scene.FindObject(id);
	SceneWorldTransform parent;
	DirectX::XMFLOAT4X4 inverse, relative;

	if (!object || !GetParentTransform(scene, object->parentId, parent) ||
	    !TransformUtils::TryInverseWorldMatrix(parent.matrix, inverse)) {
		return false;
	}

	DirectX::XMStoreFloat4x4(&relative, DirectX::XMLoadFloat4x4(&matrix) *
	                                        DirectX::XMLoadFloat4x4(&inverse));
	Transform local;
	SceneWorldTransform candidate;

	if (!TransformUtils::TryFromWorldMatrix(relative, local) ||
	    !Compose(local, &parent, candidate)) {
		return false;
	}

	object->transform = local;
	return true;
}

bool SceneHierarchy::SetWorldPosition(Scene &scene, SceneObjectId id,
                                      const DirectX::XMFLOAT3 &position) {
	auto *object = scene.FindObject(id);
	SceneWorldTransform parent;
	DirectX::XMFLOAT4X4 inverse;

	if (!object || !MathUtils::IsFinite(position) ||
	    !GetParentTransform(scene, object->parentId, parent) ||
	    !TransformUtils::TryInverseWorldMatrix(parent.matrix, inverse)) {
		return false;
	}

	auto local = object->transform;
	DirectX::XMStoreFloat3(
	    &local.translation,
	    DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&position),
	                                     DirectX::XMLoadFloat4x4(&inverse)));
	SceneWorldTransform candidate;

	if (!Compose(local, &parent, candidate)) {
		return false;
	}

	object->transform = local;
	return true;
}

bool SceneHierarchy::SetWorldPose(Scene &scene, SceneObjectId id,
                                  const DirectX::XMFLOAT3 &position,
                                  const DirectX::XMFLOAT4 &rotation) {
	using namespace DirectX;
	auto *object = scene.FindObject(id);
	SceneWorldTransform parent;
	XMFLOAT4X4 inverse;
	XMFLOAT4 normalized;
	if (!object || !MathUtils::IsFinite(position) ||
	    !TransformUtils::TryNormalizeRotation(rotation, normalized) ||
	    !GetParentTransform(scene, object->parentId, parent) ||
	    !TransformUtils::TryInverseWorldMatrix(parent.matrix, inverse)) {
		return false;
	}
	auto local = object->transform;
	XMStoreFloat3(&local.translation,
	              XMVector3TransformCoord(XMLoadFloat3(&position),
	                                      XMLoadFloat4x4(&inverse)));
	const auto relative =
	    XMMatrixRotationQuaternion(XMLoadFloat4(&normalized)) *
	    XMMatrixTranspose(
	        XMMatrixRotationQuaternion(XMLoadFloat4(&parent.rotation)));
	XMStoreFloat4(&local.rotation,
	              XMQuaternionNormalize(XMQuaternionRotationMatrix(relative)));
	SceneWorldTransform candidate;
	if (!Compose(local, &parent, candidate)) {
		return false;
	}
	object->transform = local;
	return true;
}
