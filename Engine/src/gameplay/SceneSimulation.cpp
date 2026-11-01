#include "gameplay/SceneSimulation.h"
#include "graphics/camera/Camera.h"
#include "math/TransformUtils.h"
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

/// <summary>
/// Transformの位置と回転から、+Zを前、+Yを上とする描画姿勢を作る
/// </summary>
/// <param name="transform">カメラ物体の姿勢。拡大率は描画姿勢へ適用しない</param>
/// <returns>ワールド座標のカメラ姿勢</returns>
CameraPose FromTransform(const Transform &transform) {
	using namespace DirectX;
	CameraPose pose;
	pose.position = transform.translation;
	const auto q = XMLoadFloat4(&transform.rotation);
	XMFLOAT3 forward;
	XMStoreFloat3(&forward, XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q));
	XMStoreFloat3(&pose.up, XMVector3Rotate(XMVectorSet(0, 1, 0, 0), q));
	pose.target = {pose.position.x + forward.x, pose.position.y + forward.y,
	               pose.position.z + forward.z};
	return pose;
}

/// <summary>
/// 追従結果を物体のTransformへ反映する
/// </summary>
/// <param name="pose">追従結果の位置・注視点・上方向</param>
/// <param name="transform">位置と回転を更新するTransform。拡大率は保持する</param>
/// <returns>true:成功 false:描画姿勢が無効</returns>
bool ApplyPose(const CameraPose &pose, Transform &transform) {
	Camera validator;

	if (!validator.SetPose(pose)) {
		return false;
	}

	using namespace DirectX;
	const auto view =
	    XMMatrixLookAtLH(XMLoadFloat3(&pose.position),
	                     XMLoadFloat3(&pose.target), XMLoadFloat3(&pose.up));
	const auto world = XMMatrixInverse(nullptr, view);
	XMFLOAT4 rotation;
	XMStoreFloat4(&rotation, XMQuaternionRotationMatrix(world));

	if (!TransformUtils::TryNormalizeRotation(rotation, rotation)) {
		return false;
	}

	transform.translation = pose.position;
	transform.rotation = rotation;
	return true;
}
} // namespace

bool SceneSimulation::Start(const Scene &source, std::string &error,
                            const CameraPose &fallbackPose) {
	try {
		error.clear();
		SceneObjectId character = 0, camera = 0, follow = 0;

		for (const auto &[id, object] : source.GetObjects()) {
			if (object.character) {
				if (character) {
					error = "Only one input-controlled Character Controller is "
					        "supported per scene.";
					return false;
				}

				if (!object.collider || !object.character->IsValid()) {
					error = "Character Controller requires a Box Collider and "
					        "valid movement settings.";
					return false;
				}

				character = id;
			}

			if (object.camera) {
				if (!object.camera->IsValid() ||
				    !TransformUtils::IsValid(object.transform)) {
					error = "Camera settings or transform are invalid.";
					return false;
				}

				if (object.camera->primary) {
					if (camera) {
						error = "Only one Camera can be marked Primary.";
						return false;
					}

					camera = id;
				}
			}

			if (object.followCamera) {
				const auto &settings = *object.followCamera;

				if (follow || !object.camera || object.character ||
				    !settings.IsValid() || settings.target == id ||
				    !source.Contains(settings.target)) {
					error = "Use one Follow Camera on a Camera object, with a "
					        "valid target and no Character Controller on the "
					        "same object.";
					return false;
				}

				follow = id;
			}

			if (object.respawn &&
			    (!object.character || !object.respawn->IsValid())) {
				error = "Respawn requires a Character Controller and valid "
				        "settings.";
				return false;
			}
		}

		CollisionWorld world;

		if (!world.Rebuild(source, character)) {
			error = "A scene collider cannot be represented at its transform.";
			return false;
		}

		CharacterController controller;
		DirectX::XMFLOAT3 spawn{};

		if (character) {
			const auto &object = *source.FindObject(character);

			if (!controller.Initialize(object.transform, *object.collider,
			                           *object.character, world)) {
				error =
				    "The character starts inside a collider or has an invalid "
				    "transform.";
				return false;
			}

			spawn = object.transform.translation;

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
		}

		auto objects = source.GetObjects();
		CameraPose followPose{};

		if (follow) {
			const auto &settings = *objects.at(follow).followCamera;
			followPose = MakePose(
			    objects.at(settings.target).transform.translation, settings);

			if (!ApplyPose(followPose, objects.at(follow).transform)) {
				error = "Follow Camera pose is invalid.";
				return false;
			}
		}

		auto pose =
		    camera ? FromTransform(objects.at(camera).transform) : fallbackPose;
		auto lens =
		    camera ? objects.at(camera).camera->settings : CameraSettings{};
		Camera validator;

		if (!validator.SetPose(pose) || !validator.SetSettings(lens)) {
			error = "Camera pose or projection is invalid.";
			return false;
		}

		scene_.ReplaceObjects(std::move(objects));
		world_ = std::move(world);
		controller_ = controller;
		character_ = character;
		camera_ = camera;
		follow_ = follow;
		followPose_ = followPose;
		cameraSettings_ = lens;
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
	character_ = camera_ = follow_ = 0;
}

bool SceneSimulation::Update(float seconds, const CharacterInput &input) {
	if (!playing_ || !std::isfinite(seconds) || seconds < 0) {
		return false;
	}

	auto next = controller_;
	bool respawned = false;

	if (character_) {
		if (!next.Update(seconds, input, world_)) {
			return false;
		}

		const auto *object = scene_.FindObject(character_);

		if (!object) {
			return false;
		}

		if (object->respawn &&
		    next.GetTransform().translation.y < object->respawn->minimumY) {
			if (!next.Teleport(spawn_, world_)) {
				return false;
			}

			respawned = true;
		}
	}

	auto followPose = followPose_;
	Transform followTransform;

	if (follow_) {
		const auto *cameraObject = scene_.FindObject(follow_);

		if (!cameraObject || !cameraObject->followCamera) {
			return false;
		}

		const auto &settings = *cameraObject->followCamera;
		const auto *target = scene_.FindObject(settings.target);

		if (!target) {
			return false;
		}

		const auto desired = MakePose(settings.target == character_
		                                  ? next.GetTransform().translation
		                                  : target->transform.translation,
		                              settings);
		const float blend =
		    respawned || settings.smoothing == 0
		        ? 1.0f
		        : static_cast<float>(-std::expm1(-double(settings.smoothing) *
		                                         std::min(seconds, 0.1f)));
		auto lerp = [blend](auto &a, const auto &b) {
			a.x += (b.x - a.x) * blend;
			a.y += (b.y - a.y) * blend;
			a.z += (b.z - a.z) * blend;
		};
		lerp(followPose.position, desired.position);
		lerp(followPose.target, desired.target);
		followTransform = cameraObject->transform;

		if (!ApplyPose(followPose, followTransform)) {
			return false;
		}
	}

	auto pose = pose_;

	if (camera_) {
		const auto *cameraObject = scene_.FindObject(camera_);

		if (!cameraObject || !cameraObject->camera) {
			return false;
		}

		const auto &transform = camera_ == follow_ ? followTransform
		                        : camera_ == character_
		                            ? next.GetTransform()
		                            : cameraObject->transform;
		pose = FromTransform(transform);
		Camera validator;

		if (!validator.SetPose(pose)) {
			return false;
		}
	}

	controller_ = next;

	if (character_) {
		scene_.FindObject(character_)->transform = next.GetTransform();
	}

	pose_ = pose;
	followPose_ = followPose;

	if (follow_) {
		scene_.FindObject(follow_)->transform = followTransform;
	}

	return true;
}
