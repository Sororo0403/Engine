#include "gameplay/SceneSimulation.h"
#include "gameplay/SceneCameraUtils.h"
#include "gameplay/SceneValidation.h"
#include "graphics/camera/Camera.h"
#include "math/TransformUtils.h"
#include "physics/BoxColliderUtils.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <map>
#include <set>
#include <string_view>

bool SceneSimulation::Start(const Scene &source, std::string &error,
                            const CameraPose &fallbackPose,
                            SceneObjectId *invalidObject) {
	return Configure(source, error, fallbackPose, invalidObject, false);
}

bool SceneSimulation::SetActive(SceneObjectId id, bool active,
                                std::string &error,
                                SceneObjectId *invalidObject) {
	const SceneActiveChange change{id, active};
	return SetActive(std::span<const SceneActiveChange>{&change, 1}, error,
	                 invalidObject);
}

bool SceneSimulation::SetActive(std::span<const SceneActiveChange> changes,
                                std::string &error,
                                SceneObjectId *invalidObject) {
	error.clear();
	if (invalidObject) {
		*invalidObject = 0;
	}
	if (!playing_ || changes.empty()) {
		error =
		    "Start Play and specify existing objects before changing activity.";
		return false;
	}
	try {
		Scene working;
		working.ReplaceObjects(scene_.GetObjects());
		std::set<SceneObjectId> seen;
		bool changed = false;
		for (const auto &change : changes) {
			auto *object = working.FindObject(change.objectId);
			if (!object || !seen.insert(change.objectId).second) {
				error = "Activity changes require unique, existing object IDs.";
				if (invalidObject) {
					*invalidObject = change.objectId;
				}
				return false;
			}
			changed |= object->active != change.active;
			object->active = change.active;
		}
		return !changed ||
		       Configure(working, error, fallbackPose_, invalidObject, true);
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool SceneSimulation::Configure(const Scene &source, std::string &error,
                                const CameraPose &fallbackPose,
                                SceneObjectId *invalidObject, bool preserve) {
	if (invalidObject) {
		*invalidObject = 0;
	}
	try {
		error.clear();
		const auto fail = [&](SceneObjectId id, std::string_view reason) {
			const auto *object = source.FindObject(id);
			error = std::format(
			    "{} Object: \"{}\" (ID {}).", reason,
			    object && !object->name.empty() ? object->name : "Object", id);
			if (invalidObject) {
				*invalidObject = id;
			}
			return false;
		};
		std::vector<SceneObjectId> order;
		if (!SceneCameraUtils::GetUpdateOrder(source, order, error,
		                                      invalidObject)) {
			return false;
		}
		Scene working;
		if (!preserve) {
			if (!SceneCameraUtils::ResolveInitialScene(source, working, error,
			                                           invalidObject)) {
				return false;
			}
		} else {
			working.ReplaceObjects(source.GetObjects());
			// 継続する追従は現在姿勢を保ち、新しく有効になった枝だけを依存順で初期化する。
			for (const auto id : order) {
				const auto &object = *working.FindObject(id);
				if (!SceneActivity::IsActive(working, id) ||
				    !object.followCamera || followPoses_.contains(id)) {
					continue;
				}
				SceneWorldTransform target;
				if (!SceneHierarchy::GetWorldTransform(
				        working, object.followCamera->target, target)) {
					return fail(id,
					            "Follow Camera target transform is invalid.");
				}
				auto pose = SceneCameraUtils::MakeFollowPose(
				    target.GetPosition(), *object.followCamera);
				if (!SceneCameraUtils::AvoidFollowObstacles(working, id,
				                                            pose) ||
				    !SceneCameraUtils::ApplyWorldPose(working, id, pose)) {
					return fail(
					    id, "The newly active Follow Camera pose is invalid.");
				}
			}
		}
		// 現在の姿勢を検証し、既存の追従や移動を開始時へ巻き戻さない。
		const auto issues =
		    SceneValidation::Inspect(working, fallbackPose, false);
		if (!issues.empty()) {
			error = issues.front().message;
			if (invalidObject) {
				*invalidObject = issues.front().objectId;
			}
			return false;
		}
		SceneObjectId character = 0, camera = 0;
		for (const auto &[id, object] : working.GetObjects()) {
			if (!SceneActivity::IsActive(working, id)) {
				continue;
			}
			if (object.character) {
				character = id;
			}
			if (object.camera && object.camera->primary) {
				camera = id;
			}
		}
		std::vector<SceneObjectId> ignored;
		if (character &&
		    !SceneHierarchy::GetSubtree(working, character, ignored)) {
			return fail(character, "Character hierarchy is invalid.");
		}
		CollisionWorld world;
		SceneObjectId invalidCollider = 0;
		if (!world.Rebuild(working, std::span<const SceneObjectId>{ignored},
		                   &invalidCollider)) {
			return fail(
			    invalidCollider,
			    "Box Collider cannot be represented at its world transform.");
		}
		const bool continuing =
		    preserve && character != 0 && character == character_;
		CharacterController controller;
		if (continuing) {
			controller = controller_;
		}
		DirectX::XMFLOAT3 spawn{};
		if (character) {
			const auto &object = *working.FindObject(character);
			SceneWorldTransform transform;
			if (!SceneHierarchy::GetWorldTransform(working, character,
			                                       transform) ||
			    (continuing ? (!controller.SynchronizeWorldMatrix(
			                       transform.matrix, world) ||
			                   !controller.RefreshContacts(world))
			                : !controller.Initialize(
			                      transform.matrix, *object.collider,
			                      *object.character, world))) {
				return fail(character, "The character overlaps a collider or "
				                       "has an invalid world transform.");
			}
			spawn = continuing ? spawn_ : transform.GetPosition();
			if (object.respawn) {
				const auto &settings = *object.respawn;
				if (!settings.useStartPosition) {
					spawn = settings.position;
				}
				// 復帰先の検証はコピーで行い、継続する主人公の速度と入力を消さない。
				auto placement = controller;
				if (spawn.y <= settings.minimumY ||
				    (!continuing &&
				     transform.GetPosition().y <= settings.minimumY) ||
				    !placement.Teleport(spawn, world)) {
					return fail(
					    character,
					    "The character and its respawn point must be above "
					    "Fall Below Y and outside solid colliders.");
				}
			}
		}
		std::map<SceneObjectId, CameraPose> follows;
		for (const auto id : order) {
			const auto &object = *working.FindObject(id);
			if (!SceneActivity::IsActive(working, id) || !object.followCamera) {
				continue;
			}
			if (preserve) {
				const auto found = followPoses_.find(id);
				if (found != followPoses_.end()) {
					follows.emplace(id, found->second);
					continue;
				}
			}
			SceneWorldTransform target;
			if (!SceneHierarchy::GetWorldTransform(
			        working, object.followCamera->target, target)) {
				return fail(id, "Follow Camera target transform is invalid.");
			}
			auto pose = SceneCameraUtils::MakeFollowPose(target.GetPosition(),
			                                             *object.followCamera);
			if (!SceneCameraUtils::AvoidFollowObstacles(working, id, pose)) {
				return fail(id, "Follow Camera pose is invalid.");
			}
			follows.emplace(id, pose);
		}
		auto pose = fallbackPose;
		if (camera) {
			SceneWorldTransform transform;
			if (!SceneHierarchy::GetWorldTransform(working, camera,
			                                       transform)) {
				return fail(camera, "Camera world transform is invalid.");
			}
			pose = SceneCameraUtils::FromWorldTransform(transform);
		}
		const auto lens = camera ? working.FindObject(camera)->camera->settings
		                         : CameraSettings{};
		Camera validator;
		if (!validator.SetPose(pose) || !validator.SetSettings(lens)) {
			return fail(camera, "Camera pose or projection is invalid.");
		}
		TriggerWorld triggers;
		if (preserve) {
			triggers = triggers_;
		}
		if (preserve ? !triggers.Update(working) : !triggers.Reset(working)) {
			return fail(0, "Trigger world shapes or hierarchy are invalid.");
		}
		// 全候補が有効な場合だけ確定し、失敗時は再生シーンと実行状態をすべて保持する。
		scene_.ReplaceObjects(working.GetObjects());
		world_ = std::move(world);
		triggers_ = std::move(triggers);
		controller_ = controller;
		character_ = character;
		camera_ = camera;
		updateOrder_ = std::move(order);
		followPoses_ = std::move(follows);
		cameraSettings_ = lens;
		spawn_ = spawn;
		if (!continuing) {
			respawnCount_ = 0;
			lastRespawnPosition_.reset();
		}
		pose_ = pose;
		fallbackPose_ = fallbackPose;
		playing_ = true;
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

void SceneSimulation::Stop() {
	playing_ = false;
	triggers_.Clear();
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
			if (!SceneActivity::IsActive(working, id))
				continue;
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
		std::optional<TriggerWorld> triggers;
		// 落下復帰では主人公と子孫の瞬間移動経路を検知しない。
		// 終点でのEnterと、開始時の接触に対するExitは通常どおり生成する。
		if (seconds > 0) {
			triggers = triggers_;
			if (!triggers->Update(
			        working, respawned ? std::span<const SceneObjectId>{ignored}
			                           : std::span<const SceneObjectId>{})) {
				return false;
			}
		}
		scene_.ReplaceObjects(working.GetObjects());
		if (triggers) {
			triggers_ = std::move(*triggers);
		}
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
