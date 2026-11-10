#include "scene/SceneHierarchy.h"

#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include "scene/Scene.h"

#include <cmath>
#include <set>
#include <unordered_map>

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
	enum class Visit { VISITING, COMPLETE };
	std::unordered_map<SceneObjectId, Visit> visits;
	visits.reserve(scene.GetObjects().size());
	std::vector<SceneObjectId> pending;

	for (const auto &[id, object] : scene.GetObjects()) {
		(void)object;
		auto current = id;

		while (current != 0) {
			const auto [visit, inserted] =
			    visits.try_emplace(current, Visit::VISITING);
			if (!inserted) {
				if (visit->second == Visit::VISITING)
					return false;
				break;
			}
			const auto *node = scene.FindObject(current);
			if (!node)
				return false;
			pending.push_back(current);
			current = node->parentId;
		}

		for (auto it = pending.rbegin(); it != pending.rend(); ++it) {
			visits.at(*it) = Visit::COMPLETE;
			result.push_back(*it);
		}
		pending.clear();
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

bool SceneHierarchy::LookAt(Scene &scene, SceneObjectId id,
                            const DirectX::XMFLOAT3 &target) {
	using namespace DirectX;
	auto *object = scene.FindObject(id);
	SceneWorldTransform world, parent;
	XMFLOAT4X4 inverse;
	if (!object || !MathUtils::IsFinite(target) ||
	    !GetParentTransform(scene, object->parentId, parent) ||
	    !Compose(object->transform, &parent, world) ||
	    !TransformUtils::TryInverseWorldMatrix(parent.matrix, inverse))
		return false;

	const auto position = world.GetPosition();
	const double direction[]{double(target.x) - position.x,
	                         double(target.y) - position.y,
	                         double(target.z) - position.z};
	if (direction[0] == 0 && direction[1] == 0 && direction[2] == 0)
		return true;

	// 親の拡縮・せん断も含めて方向を逆変換し、負のZ拡縮による反転を補正する。
	double localDirection[3]{};
	const double sign = object->transform.scale.z < 0 ? -1. : 1.;

	for (int axis = 0; axis < 3; ++axis) {
		for (int component = 0; component < 3; ++component)
			localDirection[axis] +=
			    direction[component] * inverse.m[component][axis] * sign;
	}

	const double length =
	    std::hypot(localDirection[0], localDirection[1], localDirection[2]);
	if (!std::isfinite(length) || length == 0)
		return false;
	const auto forward = XMVectorSet(float(localDirection[0] / length),
	                                 float(localDirection[1] / length),
	                                 float(localDirection[2] / length), 0);
	// +Yを優先し、真上・真下に近い場合は+Zを使って基底を作る。
	const auto up = std::abs(XMVectorGetY(forward)) < .9999f
	                    ? XMVectorSet(0, 1, 0, 0)
	                    : XMVectorSet(0, 0, 1, 0);
	const auto right = XMVector3Normalize(XMVector3Cross(up, forward));
	const XMMATRIX basis(right, XMVector3Cross(forward, right), forward,
	                     XMVectorSet(0, 0, 0, 1));
	auto local = object->transform;
	XMStoreFloat4(&local.rotation,
	              XMQuaternionNormalize(XMQuaternionRotationMatrix(basis)));
	if (!Compose(local, &parent, world))
		return false;
	object->transform.rotation = local.rotation;
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

bool SceneHierarchy::SetWorldPositions(
    Scene &scene, const std::map<SceneObjectId, DirectX::XMFLOAT3> &positions) {
	if (positions.empty()) {
		return false;
	}

	for (const auto &[id, position] : positions) {
		if (!scene.Contains(id) || !MathUtils::IsFinite(position)) {
			return false;
		}
	}

	return scene.ApplyTransaction([&](Scene &working) {
		std::vector<SceneObjectId> order;

		if (!GetEvaluationOrder(working, order)) {
			return false;
		}

		for (const auto id : order) {
			const auto found = positions.find(id);

			if (found == positions.end()) {
				continue;
			}

			SceneWorldTransform current;

			if (!GetWorldTransform(working, id, current)) {
				return false;
			}

			const auto position = current.GetPosition();
			const auto &desired = found->second;

			if (position.x == desired.x && position.y == desired.y &&
			    position.z == desired.z) {
				continue;
			}

			if (!SetWorldPosition(working, id, desired)) {
				return false;
			}
		}

		WorldTransforms transforms;
		return BuildWorldTransforms(working, transforms);
	});
}

bool SceneHierarchy::TranslateSelection(Scene &scene,
                                        std::span<const SceneObjectId> ids,
                                        const DirectX::XMFLOAT3 &translation) {
	std::vector<SceneObjectId> roots;
	WorldTransforms transforms;
	if (!MathUtils::IsFinite(translation) ||
	    !GetSelectionRoots(scene, ids, roots) ||
	    !BuildWorldTransforms(scene, transforms)) {
		return false;
	}

	std::map<SceneObjectId, DirectX::XMFLOAT3> positions;

	for (const auto id : roots) {
		const auto position = transforms.at(id).GetPosition();
		positions.emplace(id, DirectX::XMFLOAT3{position.x + translation.x,
		                                        position.y + translation.y,
		                                        position.z + translation.z});
	}

	return SetWorldPositions(scene, positions);
}
