#include "gameplay/SceneSimulation.h"
#include "gameplay/SceneCameraUtils.h"
#include "gameplay/SceneValidation.h"
#include "graphics/camera/Camera.h"
#include "math/TransformUtils.h"
#include "physics/BoxColliderUtils.h"
#include "scene/SceneHierarchy.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <map>
#include <string_view>

bool SceneSimulation::Start(const Scene &source, std::string &error,
                            const CameraPose &fallbackPose,
                            SceneObjectId *invalidObject) {
	if (invalidObject) {
		*invalidObject = 0;
	}

	try {
		error.clear();
		SceneObjectId character = 0, camera = 0;
		// 検証ルールは変えず、問題の物体を名前とIDで特定できるようにする。
		const auto failObject = [&](SceneObjectId id, std::string_view reason) {
			const auto *object = source.FindObject(id);
			const std::string_view name =
			    object ? std::string_view{object->name} : std::string_view{};
			error = std::format("{} Object: \"{}\" (ID {}).", reason,
			                    name.empty() ? "Object" : name, id);

			if (invalidObject) {
				*invalidObject = id;
			}

			return false;
		};

		const auto issues = SceneValidation::Inspect(source, fallbackPose);

		if (!issues.empty()) {
			error = issues.front().message;

			if (invalidObject) {
				*invalidObject = issues.front().objectId;
			}

			return false;
		}

		for (const auto &[id, object] : source.GetObjects()) {
			if (object.character) {
				character = id;
			}

			if (object.camera && object.camera->primary) {
				camera = id;
			}
		}

		std::vector<SceneObjectId> order;
		Scene working;
		if (!SceneCameraUtils::GetUpdateOrder(source, order, error,
		                                      invalidObject) ||
		    !SceneCameraUtils::ResolveInitialScene(source, working, error,
		                                           invalidObject)) {
			return false;
		}
		std::vector<SceneObjectId> ignored;
		if (character &&
		    !SceneHierarchy::GetSubtree(working, character, ignored)) {
			return failObject(character, "Character hierarchy is invalid.");
		}
		CollisionWorld world;
		SceneObjectId invalidCollider = 0;
		if (!world.Rebuild(working, std::span<const SceneObjectId>{ignored},
		                   &invalidCollider)) {
			return failObject(
			    invalidCollider,
			    "Box Collider cannot be represented at its world transform.");
		}
		CharacterController controller;
		DirectX::XMFLOAT3 spawn{};
		if (character) {
			const auto &object = *working.FindObject(character);
			SceneWorldTransform transform;
			if (!SceneHierarchy::GetWorldTransform(working, character,
			                                       transform) ||
			    !controller.Initialize(transform.matrix, *object.collider,
			                           *object.character, world)) {
				return failObject(character,
				                  "The character starts inside a collider or "
				                  "has an invalid world transform.");
			}
			spawn = transform.GetPosition();
			if (object.respawn) {
				const auto &settings = *object.respawn;
				if (!settings.useStartPosition)
					spawn = settings.position;
				if (spawn.y <= settings.minimumY ||
				    transform.GetPosition().y <= settings.minimumY ||
				    !controller.Teleport(spawn, world) ||
				    !controller.Teleport(transform.GetPosition(), world)) {
					return failObject(
					    character,
					    "The character and its respawn point must be above "
					    "Fall Below Y and outside solid colliders.");
				}
			}
		}
		std::map<SceneObjectId, CameraPose> follows;
		for (const auto id : order) {
			if (!working.FindObject(id)->followCamera)
				continue;
			SceneWorldTransform transform;
			if (!SceneHierarchy::GetWorldTransform(working, id, transform)) {
				return failObject(id,
				                  "Follow Camera world transform is invalid.");
			}
			// 注視点は回転から復元せず、Follow設定にある距離を保持する。
			const auto &object = *working.FindObject(id);
			SceneWorldTransform target;
			if (!SceneHierarchy::GetWorldTransform(
			        working, object.followCamera->target, target))
				return false;
			auto followPose = SceneCameraUtils::MakeFollowPose(
			    target.GetPosition(), *object.followCamera);
			if (!SceneCameraUtils::AvoidFollowObstacles(working, id,
			                                            followPose))
				return false;
			follows.emplace(id, followPose);
		}
		auto pose = fallbackPose;
		if (camera) {
			SceneWorldTransform transform;
			if (!SceneHierarchy::GetWorldTransform(working, camera,
			                                       transform)) {
				return failObject(camera, "Camera world transform is invalid.");
			}
			pose = SceneCameraUtils::FromWorldTransform(transform);
		}
		const auto lens = camera ? working.FindObject(camera)->camera->settings
		                         : CameraSettings{};
		Camera validator;

		if (!validator.SetPose(pose) || !validator.SetSettings(lens)) {
			if (camera) {
				return failObject(camera,
				                  "Camera pose or projection is invalid.");
			}
			error = "Fallback Camera pose or projection is invalid.";
			return false;
		}

		scene_.ReplaceObjects(working.GetObjects());
		world_ = std::move(world);
		controller_ = controller;
		character_ = character;
		camera_ = camera;
		updateOrder_ = std::move(order);
		followPoses_ = std::move(follows);
		cameraSettings_ = lens;
		spawn_ = spawn;
		respawnCount_ = 0;
		lastRespawnPosition_.reset();
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
	updateOrder_.clear();
	followPoses_.clear();
	respawnCount_ = 0;
	lastRespawnPosition_.reset();
}

bool SceneSimulation::Update(float seconds, const CharacterInput &input) {
	if (!playing_ || !std::isfinite(seconds) || seconds < 0)
		return false;
	try {
		Scene working;
		working.ReplaceObjects(scene_.GetObjects());
		auto next = controller_;
		auto follows = followPoses_;
		CollisionWorld world;
		std::vector<SceneObjectId> ignored;
		if (character_ &&
		    !SceneHierarchy::GetSubtree(working, character_, ignored))
			return false;
		bool respawned = false;
		// 親と追従対象を先に更新する。カメラを親にした主人公も同じ依存順で処理する。
		for (const auto id : updateOrder_) {
			const auto *object = working.FindObject(id);
			if (!object)
				return false;
			if (id == character_) {
				SceneWorldTransform transform;
				if (!object->collider || !object->character ||
				    !world.Rebuild(working,
				                   std::span<const SceneObjectId>{ignored}) ||
				    !SceneHierarchy::GetWorldTransform(working, id,
				                                       transform) ||
				    !next.SynchronizeWorldMatrix(transform.matrix, world) ||
				    !next.Update(seconds, input, world))
					return false;
				if (object->respawn && next.GetTransform().translation.y <
				                           object->respawn->minimumY) {
					if (!next.Teleport(spawn_, world))
						return false;
					respawned = true;
				}
				if (!SceneHierarchy::SetWorldPosition(
				        working, id, next.GetTransform().translation))
					return false;
			}
			if (object->followCamera) {
				const auto &settings = *object->followCamera;
				const auto found = follows.find(id);
				SceneWorldTransform target;
				if (!object->camera || found == follows.end() ||
				    !SceneHierarchy::GetWorldTransform(working, settings.target,
				                                       target))
					return false;
				const auto desired = SceneCameraUtils::MakeFollowPose(
				    target.GetPosition(), settings);
				const float blend = respawned || settings.smoothing == 0
				                        ? 1.0f
				                        : static_cast<float>(-std::expm1(
				                              -double(settings.smoothing) *
				                              std::min(seconds, 0.1f)));
				const auto lerp = [blend](auto &a, const auto &b) {
					a.x += (b.x - a.x) * blend;
					a.y += (b.y - a.y) * blend;
					a.z += (b.z - a.z) * blend;
				};
				const auto previous = found->second;
				auto &pose = found->second;
				lerp(pose.position, desired.position);
				lerp(pose.target, desired.target);
				if (!SceneCameraUtils::AvoidFollowObstacles(working, id,
				                                            pose)) {
					pose.position = previous.position;
					Camera validator;
					if (!validator.SetPose(pose))
						pose = previous;
				}
				if (!SceneCameraUtils::ApplyWorldPose(working, id, pose))
					return false;
			}
		}
		auto pose = pose_;
		if (camera_) {
			SceneWorldTransform transform;
			if (!SceneHierarchy::GetWorldTransform(working, camera_, transform))
				return false;
			pose = SceneCameraUtils::FromWorldTransform(transform);
			Camera validator;
			if (!validator.SetPose(pose))
				return false;
		}
		// 全更新の成功後に確定し、失敗したフレームは位置・速度・追従状態を保持する。
		if (!world.Rebuild(working, std::span<const SceneObjectId>{ignored}))
			return false;
		if (character_) {
			SceneWorldTransform transform;
			if (!SceneHierarchy::GetWorldTransform(working, character_,
			                                       transform) ||
			    !next.SynchronizeWorldMatrix(transform.matrix, world))
				return false;
		}
		scene_.ReplaceObjects(working.GetObjects());
		controller_ = next;
		world_ = std::move(world);
		followPoses_ = std::move(follows);
		pose_ = pose;
		if (respawned) {
			++respawnCount_;
			lastRespawnPosition_ = next.GetTransform().translation;
		}
		return true;
	} catch (const std::exception &) {
		return false;
	}
}

std::optional<CharacterRuntimeState>
SceneSimulation::GetCharacterState() const {
	if (!playing_ || character_ == 0) {
		return std::nullopt;
	}

	SceneObjectId groundObject = 0;

	if (controller_.IsGrounded()) {
		controller_.FindGroundObject(world_, groundObject);
	}

	return CharacterRuntimeState{character_,
	                             controller_.GetTransform().translation,
	                             controller_.GetVerticalVelocity(),
	                             controller_.IsGrounded(),
	                             respawnCount_,
	                             lastRespawnPosition_,
	                             groundObject};
}
