#include "gameplay/StageFlowPrefab.h"

#include "physics/ColliderUtils.h"
#include "scene/PrefabInstantiation.h"
#include "scene/PrefabSerializer.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"

#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace StageFlow {
namespace {
using namespace DirectX;

/// <summary>
/// 接続点の+Zを区間の外向きとして読み、水平な90度単位の向きに変換する
/// </summary>
unsigned ReadTurn(const SceneWorldTransform &world, std::string_view name) {
	const auto rotation = XMLoadFloat4(&world.rotation);
	XMFLOAT3 up;
	XMFLOAT3 forward;
	XMStoreFloat3(&up, XMVector3Rotate(XMVectorSet(0, 1, 0, 0), rotation));
	XMStoreFloat3(&forward, XMVector3Rotate(XMVectorSet(0, 0, 1, 0), rotation));
	constexpr std::array<XMFLOAT3, 4> directions{{
	    {0, 0, 1}, {1, 0, 0}, {0, 0, -1}, {-1, 0, 0},
	}};
	constexpr float tolerance = 0.0002f;
	if (std::abs(up.x) <= tolerance && std::abs(up.y - 1) <= tolerance &&
	    std::abs(up.z) <= tolerance && std::abs(forward.y) <= tolerance) {
		for (unsigned i = 0; i < directions.size(); ++i) {
			if (std::abs(forward.x - directions[i].x) <= tolerance &&
			    std::abs(forward.z - directions[i].z) <= tolerance) {
				return i;
			}
		}
	}
	throw std::runtime_error("Port '" + std::string(name) + "' needs an upright 90-degree heading.");
}
} // namespace

bool ReadModule(const Scene &prefab, std::string_view id, std::string_view sectionType,
                Module &module, std::string &error) {
	try {
		SceneObjectId root = 0;
		if (!PrefabSerializer::Validate(prefab, root, error)) {
			return false;
		}
		SceneHierarchy::WorldTransforms transforms;
		SceneActivity::States activity;
		if (!SceneHierarchy::BuildWorldTransforms(prefab, transforms) ||
		    !SceneActivity::BuildStates(prefab, activity)) {
			throw std::runtime_error("Section prefab has invalid transforms.");
		}
		Module candidate;
		candidate.id = id;
		candidate.sectionType = sectionType;
		for (const auto &[objectId, object] : prefab.GetObjects()) {
			const auto &world = transforms.at(objectId);
			const std::string_view tag = object.tag;
			const bool entry = tag.starts_with("stage-entry:");
			if (entry || tag.starts_with("stage-exit:")) {
				const auto kind = tag.substr(tag.find(':') + 1);
				if (kind != "walk" && kind != "jump") {
					throw std::runtime_error("Use walk or jump as the port type on '" + object.name + "'.");
				}
				auto &ports = entry ? candidate.entries : candidate.exits;
				ports.push_back({object.name, kind == "walk" ? PortKind::WALK : PortKind::JUMP,
				                 {world.GetPosition(), ReadTurn(world, object.name)}});
			}
			if (activity.at(objectId) && object.collider && !object.collider->isTrigger &&
			    !object.character) {
				AxisAlignedBox bounds;
				if (!ColliderUtils::GetWorldBounds(*object.collider, world.matrix, bounds)) {
					throw std::runtime_error("Invalid collider on '" + object.name + "'.");
				}
				candidate.bounds.push_back(bounds);
			}
		}
		if (candidate.bounds.empty()) {
			throw std::runtime_error("Section prefab needs a solid collider.");
		}
		module = std::move(candidate);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

SceneObjectId PlaceModule(const Scene &prefab, const Pose &pose, Scene &destination,
                          std::string &error) {
	try {
		SceneObjectId root = 0;
		if (!PrefabSerializer::Validate(prefab, root, error)) {
			return 0;
		}
		Scene rotated;
		rotated.CopyFrom(prefab);
		auto *object = rotated.FindObject(root);
		const Pose heading{{}, pose.quarterTurns};
		const auto yaw = XMQuaternionRotationRollPitchYaw(
		    0.0f, static_cast<float>(pose.quarterTurns % 4) * XM_PIDIV2, 0.0f);
		XMStoreFloat4(&object->transform.rotation,
		              XMQuaternionNormalize(XMQuaternionMultiply(
		                  XMLoadFloat4(&object->transform.rotation), yaw)));
		const auto original = object->transform.translation;
		object->transform.translation = Compose({original}, heading).position;
		// 通常のPrefab配置は復帰先を平行移動する。回転分を先に反映する。
		for (const auto &[id, source] : prefab.GetObjects()) {
			if (source.respawn && !source.respawn->useStartPosition) {
				rotated.FindObject(id)->respawn->position =
				    Compose({source.respawn->position}, heading).position;
			}
		}
		return PrefabInstantiation::Instantiate(
		    rotated, destination, Compose({original}, pose).position, error);
	} catch (const std::exception &exception) {
		error = exception.what();
		return 0;
	}
}
} // namespace StageFlow
