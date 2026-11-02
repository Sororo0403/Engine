#include "gameplay/SceneValidation.h"

#include "gameplay/CharacterController.h"
#include "gameplay/SceneCameraUtils.h"
#include "graphics/camera/Camera.h"
#include "math/TransformUtils.h"
#include "physics/BoxColliderUtils.h"
#include "physics/CollisionWorld.h"
#include "scene/Scene.h"
#include <format>
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

	for (const auto &[id, object] : scene.GetObjects()) {
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

			if (!object.collider || !object.character->IsValid()) {
				add(id, "Character Controller requires a Box Collider and "
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

		if (object.collider && !BoxColliderUtils::GetWorldBounds(
		                           *object.collider, object.transform, body)) {
			add(id, "Box Collider cannot be represented at its transform.");
		}

		if (object.followCamera) {
			Transform resolved;

			if (!SceneCameraUtils::GetInitialTransform(scene, id, resolved)) {
				add(id, "Follow Camera requires valid Camera settings, a valid "
				        "target and pose, and no circular or broken chain or "
				        "Character Controller on the same object.");
			}
		}
	}

	for (const auto &[id, object] : scene.GetObjects()) {
		if (!object.character || !object.character->IsValid() ||
		    !object.collider) {
			continue;
		}

		CollisionWorld world;

		if (!world.Rebuild(scene, id)) {
			// 不正な障害物は上の一覧で報告し、修正後に主人公の配置を再検証する。
			continue;
		}

		CharacterController controller;

		if (!controller.Initialize(object.transform, *object.collider,
		                           *object.character, world)) {
			add(id, "The character starts inside a collider or has an invalid "
			        "transform or Box Collider.");
		}

		if (object.respawn && object.respawn->IsValid()) {
			const auto &settings = *object.respawn;
			auto recovery = object.transform;

			if (!settings.useStartPosition) {
				recovery.translation = settings.position;
			}

			if (object.transform.translation.y <= settings.minimumY ||
			    recovery.translation.y <= settings.minimumY ||
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
