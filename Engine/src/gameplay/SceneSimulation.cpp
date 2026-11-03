#include "gameplay/SceneSimulation.h"
#include "gameplay/SceneCameraUtils.h"
#include "gameplay/SceneValidation.h"
#include "graphics/camera/Camera.h"
#include "math/TransformUtils.h"
#include "physics/BoxColliderUtils.h"
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

		std::vector<SceneObjectId> followOrder;

		if (!SceneCameraUtils::GetFollowOrder(source, followOrder, error,
		                                      invalidObject)) {
			return false;
		}

		CollisionWorld world;

		SceneObjectId invalidCollider = 0;
		if (!world.Rebuild(source, character, &invalidCollider)) {
			return failObject(
			    invalidCollider,
			    "Box Collider cannot be represented at its transform.");
		}

		CharacterController controller;
		DirectX::XMFLOAT3 spawn{};

		if (character) {
			const auto &object = *source.FindObject(character);

			if (!controller.Initialize(object.transform, *object.collider,
			                           *object.character, world)) {
				return failObject(
				    character,
				    "The character starts inside a collider or has an invalid "
				    "transform or Box Collider.");
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
					return failObject(
					    character,
					    "The character and its respawn point must be above "
					    "Fall Below Y and outside solid colliders.");
				}

				if (!controller.Teleport(object.transform.translation, world)) {
					return failObject(
					    character,
					    "Could not restore the character start position.");
				}
			}
		}

		auto objects = source.GetObjects();
		std::vector<FollowState> follows;
		follows.reserve(followOrder.size());
		std::map<SceneObjectId, std::size_t> indices;
		std::optional<std::size_t> cameraFollow;

		for (const auto id : followOrder) {
			auto &object = objects.at(id);
			const auto &settings = *object.followCamera;
			auto followPose = SceneCameraUtils::MakeFollowPose(
			    objects.at(settings.target).transform.translation, settings);

			if (!SceneCameraUtils::AvoidFollowObstacles(source, id,
			                                            followPose) ||
			    !SceneCameraUtils::ApplyPose(followPose, object.transform)) {
				return failObject(id, "Follow Camera pose is invalid.");
			}

			std::optional<std::size_t> parent;

			if (const auto found = indices.find(settings.target);
			    found != indices.end()) {
				parent = found->second;
			}

			if (id == camera) {
				cameraFollow = follows.size();
			}

			indices.emplace(id, follows.size());
			follows.push_back(
			    {id, settings.target, parent, followPose, object.transform});
		}

		auto nextFollows = follows;

		auto pose =
		    camera
		        ? SceneCameraUtils::FromTransform(objects.at(camera).transform)
		        : fallbackPose;
		auto lens =
		    camera ? objects.at(camera).camera->settings : CameraSettings{};
		Camera validator;

		if (!validator.SetPose(pose) || !validator.SetSettings(lens)) {
			if (camera) {
				return failObject(camera,
				                  "Camera pose or projection is invalid.");
			}
			error = "Fallback Camera pose or projection is invalid.";
			return false;
		}

		scene_.ReplaceObjects(std::move(objects));
		world_ = std::move(world);
		controller_ = controller;
		character_ = character;
		camera_ = camera;
		cameraFollow_ = cameraFollow;
		follows_ = std::move(follows);
		nextFollows_ = std::move(nextFollows);
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
	character_ = camera_ = 0;
	cameraFollow_.reset();
	follows_.clear();
	nextFollows_.clear();
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

	std::copy(follows_.begin(), follows_.end(), nextFollows_.begin());

	for (std::size_t i = 0; i < nextFollows_.size(); ++i) {
		auto &state = nextFollows_[i];
		const auto *cameraObject = scene_.FindObject(state.objectId);
		const auto *target = scene_.FindObject(state.targetId);

		if (!cameraObject || !cameraObject->camera ||
		    !cameraObject->followCamera || !target ||
		    cameraObject->followCamera->target != state.targetId ||
		    (state.targetFollow && *state.targetFollow >= i)) {
			return false;
		}

		const auto &settings = *cameraObject->followCamera;
		const auto targetPosition =
		    state.targetFollow
		        ? nextFollows_[*state.targetFollow].transform.translation
		    : state.targetId == character_ ? next.GetTransform().translation
		                                   : target->transform.translation;
		const auto desired =
		    SceneCameraUtils::MakeFollowPose(targetPosition, settings);
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
		const auto previousPose = state.pose;
		lerp(state.pose.position, desired.position);
		lerp(state.pose.target, desired.target);
		if (!SceneCameraUtils::AvoidFollowObstacles(scene_, state.objectId,
		                                            state.pose)) {
			// 位置を安全な場所に保ち、注視点は追従させて障害物の外へ抜けられるようにする。
			state.pose.position = previousPose.position;
			Camera validator;
			if (!validator.SetPose(state.pose)) {
				state.pose = previousPose;
			}
		}
		state.transform = cameraObject->transform;

		if (!SceneCameraUtils::ApplyPose(state.pose, state.transform)) {
			return false;
		}
	}

	auto pose = pose_;

	if (camera_) {
		const auto *cameraObject = scene_.FindObject(camera_);

		if (!cameraObject || !cameraObject->camera) {
			return false;
		}

		const auto &transform =
		    cameraFollow_           ? nextFollows_[*cameraFollow_].transform
		    : camera_ == character_ ? next.GetTransform()
		                            : cameraObject->transform;
		pose = SceneCameraUtils::FromTransform(transform);
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
	follows_.swap(nextFollows_);

	for (const auto &state : follows_) {
		scene_.FindObject(state.objectId)->transform = state.transform;
	}

	return true;
}

std::optional<CharacterRuntimeState>
SceneSimulation::GetCharacterState() const {
	if (!playing_ || character_ == 0) {
		return std::nullopt;
	}

	return CharacterRuntimeState{
	    character_, controller_.GetTransform().translation,
	    controller_.GetVerticalVelocity(), controller_.IsGrounded()};
}
