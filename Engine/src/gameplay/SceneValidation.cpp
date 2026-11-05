#include "gameplay/SceneValidation.h"

#include "gameplay/CharacterController.h"
#include "gameplay/SceneCameraUtils.h"
#include "graphics/camera/Camera.h"
#include "math/TransformUtils.h"
#include "physics/BoxColliderUtils.h"
#include "physics/CollisionWorld.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include <format>
#include <map>
#include <optional>
#include <string_view>

std::vector<SceneValidationIssue>
SceneValidation::Inspect(const Scene &scene, const CameraPose &fallbackPose) {
	std::vector<SceneValidationIssue> issues;
	const auto add = [&](SceneObjectId id, std::string_view reason) {
		const auto *object = scene.FindObject(id);
		issues.push_back(
		    {id, object
		             ? std::format(
		                   "{} Object: \"{}\" (ID {}).", reason,
		                   object->name.empty() ? "Object" : object->name, id)
		             : std::string(reason)});
	};
	SceneObjectId firstCharacter = 0, primary = 0;
	SceneActivity::States active;
	if (!SceneActivity::BuildStates(scene, active)) {
		add(0, "Parent hierarchy is invalid.");
		return issues;
	}

	SceneHierarchy::WorldTransforms sourceTransforms;
	if (!SceneHierarchy::BuildWorldTransforms(scene, sourceTransforms)) {
		add(0, "Parent hierarchy or composed world transform is invalid.");
		return issues;
	}
	Scene resolved;
	std::string followError;
	const bool resolvedInitial =
	    SceneCameraUtils::ResolveInitialScene(scene, resolved, followError);
	const auto &initial = resolvedInitial ? resolved : scene;
	SceneHierarchy::WorldTransforms transforms;
	if (!SceneHierarchy::BuildWorldTransforms(initial, transforms)) {
		add(0, "Initial world transforms are invalid.");
		return issues;
	}
	for (const auto &[id, object] : scene.GetObjects()) {
		if (!active.at(id)) {
			continue;
		}
		if ((object.mesh && !object.mesh->IsValid()) ||
		    (object.meshRenderer && !object.meshRenderer->IsValid())) {
			add(id, "Mesh or Mesh Renderer settings are invalid.");
		}

		if (object.colorTint && !object.colorTint->IsValid()) {
			add(id, "Color Tint settings are invalid.");
		}

		if (object.character) {
			if (firstCharacter) {
				add(id, std::format(
				            "Only one input-controlled Character Controller is "
				            "supported per scene. Conflicts with object ID {}.",
				            firstCharacter));
			} else {
				firstCharacter = id;
			}

			if (!object.collider || object.collider->isTrigger ||
			    !object.character->IsValid()) {
				add(id, "Character Controller requires a non-trigger Box "
				        "Collider and "
				        "valid movement settings.");
			}
		}

		if (object.camera) {
			if (!object.camera->IsValid() ||
			    !TransformUtils::IsValid(object.transform)) {
				add(id, "Camera settings or transform are invalid.");
			}

			if (object.camera->primary) {
				if (primary) {
					add(id, std::format("Only one Camera can be marked "
					                    "Primary. Conflicts with object ID {}.",
					                    primary));
				} else {
					primary = id;
				}
			}
		}

		if (object.respawn &&
		    (!object.character || !object.respawn->IsValid())) {
			add(id,
			    "Respawn requires a Character Controller and valid settings.");
		}

		AxisAlignedBox body;

		if (object.collider &&
		    !BoxColliderUtils::GetWorldBounds(*object.collider,
		                                      transforms.at(id).matrix, body)) {
			add(id, "Box Collider cannot be represented at its transform.");
		}

		if (object.followCamera) {
			Transform cameraTransform;

			// 不正な依存関係は、個別検証で各物体へ報告する。
			const bool valid =
			    resolvedInitial || SceneCameraUtils::GetInitialTransform(
			                           scene, id, cameraTransform);

			if (!valid) {
				add(id, "Follow Camera requires valid Camera settings, a valid "
				        "target and pose, and no circular or broken chain or "
				        "Character Controller on the same object.");
			}
		}
	}

	for (const auto &[id, object] : scene.GetObjects()) {
		if (!active.at(id) || !object.character ||
		    !object.character->IsValid() || !object.collider) {
			continue;
		}

		CollisionWorld world;

		std::vector<SceneObjectId> ignored;
		if (!SceneHierarchy::GetSubtree(initial, id, ignored) ||
		    !world.Rebuild(initial, std::span<const SceneObjectId>{ignored})) {
			// 不正な障害物は上の一覧で報告し、修正後に主人公の配置を再検証する。
			continue;
		}

		CharacterController controller;

		if (!controller.Initialize(transforms.at(id).matrix, *object.collider,
		                           *object.character, world)) {
			add(id, "The character starts inside a collider or has an invalid "
			        "transform or Box Collider.");
		}

		if (object.respawn && object.respawn->IsValid()) {
			const auto &settings = *object.respawn;
			auto recovery = transforms.at(id).matrix;

			if (!settings.useStartPosition) {
				recovery._41 = settings.position.x;
				recovery._42 = settings.position.y;
				recovery._43 = settings.position.z;
			}

			if (transforms.at(id).GetPosition().y <= settings.minimumY ||
			    recovery._42 <= settings.minimumY ||
			    !controller.Initialize(recovery, *object.collider,
			                           *object.character, world)) {
				add(id, "The character and its respawn point must be above "
				        "Fall Below Y and outside solid colliders.");
			}
		}
	}

	Camera validator;

	if (primary) {
		const auto &object = *scene.FindObject(primary);
		CameraPose pose;

		if (!SceneCameraUtils::GetInitialPose(scene, primary, pose) ||
		    !validator.SetPose(pose) ||
		    !validator.SetSettings(object.camera->settings)) {
			add(primary, "Camera pose or projection is invalid.");
		}
	} else if (!validator.SetPose(fallbackPose) ||
	           !validator.SetSettings(CameraSettings{})) {
		add(0, "Fallback Camera pose or projection is invalid.");
	}

	return issues;
}
