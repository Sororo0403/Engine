#include "gameplay/SceneSimulation.h"
#include "graphics/camera/Camera.h"
#include "physics/BoxColliderUtils.h"
#include <algorithm>
#include <cmath>
#include <exception>

namespace {
/// <summary>
/// 追従対象からカメラ位置と注視点を計算する
/// </summary>
/// <param name="target">対象位置</param>
/// <param name="settings">カメラ設定</param>
/// <returns>ワールド座標のカメラ姿勢</returns>
CameraPose MakePose(const DirectX::XMFLOAT3 &target,
                    const FollowCameraSettings &settings) {
	CameraPose result;
	result.position = {target.x + settings.offset.x,
	                   target.y + settings.offset.y,
	                   target.z + settings.offset.z};
	result.target = {target.x + settings.lookOffset.x,
	                 target.y + settings.lookOffset.y,
	                 target.z + settings.lookOffset.z};
	return result;
}
} // namespace

bool SceneSimulation::Start(const Scene &source, std::string &error) {
	try {
		error.clear();
		SceneObjectId character = 0, camera = 0;
		for (const auto &[id, object] : source.GetObjects()) {
			if (object.character) {
				if (character) {
					error = "Only one input-controlled Character Controller is "
					        "supported per scene.";
					return false;
				}
				if (!object.collider || !object.character->IsValid()) {
					error = "Character Controller requires a valid Box "
					        "Collider and movement settings.";
					return false;
				}
				character = id;
			}
			if (object.followCamera) {
				const auto &settings = *object.followCamera;
				if (camera || !settings.IsValid() || settings.target == id ||
				    !source.Contains(settings.target)) {
					error = "Use one Follow Camera with a valid target in this "
					        "scene.";
					return false;
				}
				camera = id;
			}
			if (object.respawn &&
			    (!object.character || !object.respawn->IsValid())) {
				error = "Respawn requires a Character Controller and valid "
				        "settings.";
				return false;
			}
		}
		if (!character || !camera) {
			error = "Add a Character Controller and a Follow Camera, then "
			        "choose the camera target.";
			return false;
		}
		CollisionWorld world;
		if (!world.Rebuild(source, character)) {
			error = "A scene collider cannot be represented at its transform.";
			return false;
		}
		const auto &object = *source.FindObject(character);
		CharacterController controller;
		if (!controller.Initialize(object.transform, *object.collider,
		                           *object.character, world)) {
			error = "The character starts inside a collider or has an invalid "
			        "transform.";
			return false;
		}
		auto spawn = object.transform.translation;
		if (object.respawn) {
			const auto &settings = *object.respawn;
			if (!settings.useStartPosition) {
				spawn = settings.position;
			}
			if (spawn.y <= settings.minimumY ||
			    object.transform.translation.y <= settings.minimumY ||
			    !controller.Teleport(spawn, world)) {
				error = "The character and its respawn point must be above "
				        "Fall Below Y and outside solid colliders.";
				return false;
			}
			if (!controller.Teleport(object.transform.translation, world)) {
				return false;
			}
		}
		const auto &cameraSettings = *source.FindObject(camera)->followCamera;
		const auto pose = MakePose(
		    source.FindObject(cameraSettings.target)->transform.translation,
		    cameraSettings);
		Camera validator;
		if (!validator.SetPose(pose)) {
			error = "The Follow Camera pose cannot be represented. Adjust "
			        "offsets or target position.";
			return false;
		}
		auto objects = source.GetObjects();
		scene_.ReplaceObjects(std::move(objects));
		world_ = std::move(world);
		controller_ = controller;
		character_ = character;
		camera_ = camera;
		spawn_ = spawn;
		pose_ = pose;
		playing_ = true;
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
void SceneSimulation::Stop() {
	playing_ = false;
	scene_.ReplaceObjects({});
	character_ = camera_ = 0;
}
bool SceneSimulation::Update(float seconds, const CharacterInput &input) {
	if (!playing_ || !std::isfinite(seconds) || seconds < 0) {
		return false;
	}
	auto next = controller_;
	if (!next.Update(seconds, input, world_)) {
		return false;
	}
	const auto *object = scene_.FindObject(character_);
	if (!object) {
		return false;
	}
	bool respawned = false;
	if (object->respawn &&
	    next.GetTransform().translation.y < object->respawn->minimumY) {
		if (!next.Teleport(spawn_, world_)) {
			return false;
		}
		respawned = true;
	}
	const auto *cameraObject = scene_.FindObject(camera_);
	if (!cameraObject || !cameraObject->followCamera) {
		return false;
	}
	const auto &settings = *cameraObject->followCamera;
	const auto *target = scene_.FindObject(settings.target);
	if (!target) {
		return false;
	}
	const auto desired =
	    MakePose(settings.target == character_ ? next.GetTransform().translation
	                                           : target->transform.translation,
	             settings);
	const float blend =
	    respawned || settings.smoothing == 0
	        ? 1.0f
	        : static_cast<float>(-std::expm1(-double(settings.smoothing) *
	                                         std::min(seconds, 0.1f)));
	auto pose = pose_;
	auto lerp = [blend](auto &a, const auto &b) {
		a.x += (b.x - a.x) * blend;
		a.y += (b.y - a.y) * blend;
		a.z += (b.z - a.z) * blend;
	};
	lerp(pose.position, desired.position);
	lerp(pose.target, desired.target);
	Camera validator;
	if (!validator.SetPose(pose)) {
		return false;
	}
	controller_ = next;
	scene_.FindObject(character_)->transform = next.GetTransform();
	pose_ = pose;
	return true;
}
