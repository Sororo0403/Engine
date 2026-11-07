#include "gameplay/SceneCameraUtils.h"
#include "gameplay/SceneSimulation.h"

#include "math/TransformUtils.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <iterator>
#include <utility>

namespace {
/// <summary>
/// 進入・退出・操作入力を発生させた相手が、操作の接触条件を満たすか確認する
/// </summary>
bool MatchesContact(const TriggerAction &action, const SceneObject *other) {
	if (!action.contactTag.empty() &&
	    (!other || other->tag != action.contactTag))
		return false;
	switch (action.contact) {
	case TriggerAction::Contact::ANY:
		return true;
	case TriggerAction::Contact::CHARACTER:
		return other && other->character.has_value();
	case TriggerAction::Contact::DYNAMIC_BODY:
		return other && other->rigidBody && other->rigidBody->enabled &&
		       !other->rigidBody->isKinematic;
	}
	return false;
}
} // namespace

void SceneSimulation::CopyFrom(const SceneSimulation &source) {
	scene_.CopyFrom(source.scene_);
	sharedScriptData_ = source.sharedScriptData_;
	world_ = source.world_;
	triggers_ = source.triggers_;
	collisions_ = source.collisions_;
	controller_ = source.controller_;
	motion_ = source.motion_;
	animations_ = source.animations_;
	particles_ = source.particles_;
	animators_ = source.animators_;
	loadAnimatorModel_ = source.loadAnimatorModel_;
	physics_ = source.physics_;
	scripts_ = source.scripts_;
	ui_ = source.ui_;
	character_ = source.character_;
	camera_ = source.camera_;
	updateOrder_ = source.updateOrder_;
	followPoses_ = source.followPoses_;
	orbitStates_ = source.orbitStates_;
	spawn_ = source.spawn_;
	respawnCount_ = source.respawnCount_;
	lastRespawnPosition_ = source.lastRespawnPosition_;
	pose_ = source.pose_;
	fallbackPose_ = source.fallbackPose_;
	cameraSettings_ = source.cameraSettings_;
	preparePrefabResources_ = source.preparePrefabResources_;
	playing_ = source.playing_;
	frameEvents_ = source.frameEvents_;
	audioActions_ = source.audioActions_;
	audioRevision_ = source.audioRevision_;
	pendingActionSignals_ = source.pendingActionSignals_;
	firedActions_ = source.firedActions_;
	occupiedActions_ = source.occupiedActions_;
	lastUpdateError_ = source.lastUpdateError_;
	pendingScene_ = source.pendingScene_;
	sceneRevision_ = source.sceneRevision_;
}

void SceneSimulation::Adopt(SceneSimulation &source) {
	// シーンの複製と検証が完了してから、確保を伴わない移動で残りの状態を確定する。
	scene_.CopyFrom(source.scene_);
	sharedScriptData_ = std::move(source.sharedScriptData_);
	world_ = std::move(source.world_);
	triggers_ = std::move(source.triggers_);
	collisions_ = std::move(source.collisions_);
	controller_ = source.controller_;
	motion_ = std::move(source.motion_);
	animations_ = std::move(source.animations_);
	particles_ = std::move(source.particles_);
	animators_ = std::move(source.animators_);
	loadAnimatorModel_ = std::move(source.loadAnimatorModel_);
	physics_ = std::move(source.physics_);
	scripts_ = std::move(source.scripts_);
	ui_ = std::move(source.ui_);
	character_ = source.character_;
	camera_ = source.camera_;
	updateOrder_ = std::move(source.updateOrder_);
	followPoses_ = std::move(source.followPoses_);
	orbitStates_ = std::move(source.orbitStates_);
	spawn_ = source.spawn_;
	respawnCount_ = source.respawnCount_;
	lastRespawnPosition_ = source.lastRespawnPosition_;
	pose_ = source.pose_;
	fallbackPose_ = source.fallbackPose_;
	cameraSettings_ = source.cameraSettings_;
	preparePrefabResources_ = source.preparePrefabResources_;
	playing_ = source.playing_;
	frameEvents_ = std::move(source.frameEvents_);
	audioActions_ = std::move(source.audioActions_);
	audioRevision_ = source.audioRevision_;
	pendingActionSignals_ = std::move(source.pendingActionSignals_);
	firedActions_ = std::move(source.firedActions_);
	occupiedActions_ = std::move(source.occupiedActions_);
	lastUpdateError_ = std::move(source.lastUpdateError_);
	pendingScene_ = std::move(source.pendingScene_);
	sceneRevision_ = source.sceneRevision_;
}

void SceneSimulation::QueueActionSignals(
    std::span<const TriggerEvent> events,
    std::vector<ActionSignal> &signals) const {
	for (const auto &event : events) {
		if (event.type == TriggerEvent::Type::STAY) {
			continue;
		}
		for (const auto id :
		     std::array{event.contact.first, event.contact.second}) {
			const auto *object = scene_.FindObject(id);
			if (object && SceneActivity::IsActive(scene_, id) &&
			    object->collider && object->collider->isTrigger &&
			    object->triggerActions &&
			    !object->triggerActions->actions.empty()) {
				signals.push_back({id,
				                   event.type == TriggerEvent::Type::ENTER
				                       ? TriggerAction::Event::ENTER
				                       : TriggerAction::Event::EXIT,
				                   id == event.contact.first
				                       ? event.contact.second
				                       : event.contact.first});
			}
		}
	}
}

void SceneSimulation::QueueOccupancySignals(
    std::vector<ActionSignal> &signals,
    std::set<std::pair<SceneObjectId, std::size_t>> &occupied) const {
	for (const auto &contact : triggers_.GetContacts()) {
		for (const auto id : std::array{contact.first, contact.second}) {
			const auto *object = scene_.FindObject(id);
			if (!object || !object->collider || !object->collider->isTrigger ||
			    !object->triggerActions)
				continue;
			const auto other =
			    id == contact.first ? contact.second : contact.first;
			const auto &actions = object->triggerActions->actions;
			for (std::size_t i = 0; i < actions.size(); ++i) {
				const auto &action = actions[i];
				if (!action.UsesOccupancy() ||
				    (action.operation ==
				         TriggerAction::Operation::SET_CHECKPOINT &&
				     other != action.target) ||
				    !MatchesContact(action, scene_.FindObject(other)))
					continue;
				occupied.emplace(id, i);
			}
		}
	}
	std::vector<std::pair<SceneObjectId, std::size_t>> changed;
	std::set_symmetric_difference(occupiedActions_.begin(),
	                              occupiedActions_.end(), occupied.begin(),
	                              occupied.end(), std::back_inserter(changed));
	for (const auto &[id, index] : changed) {
		const auto *object = scene_.FindObject(id);
		if (!object || !object->triggerActions ||
		    index >= object->triggerActions->actions.size() ||
		    !SceneActivity::IsActive(scene_, id))
			continue;
		const auto &action = object->triggerActions->actions[index];
		const auto event = occupied.contains({id, index})
		                       ? TriggerAction::Event::FIRST_ENTER
		                       : TriggerAction::Event::LAST_EXIT;
		if (action.event == event)
			signals.push_back({id, event, action.target, index});
	}
}

bool SceneSimulation::ApplyTriggerActions(
    std::string &error, SceneObjectId *invalidObject, bool interactPressed,
    std::span<const GameActionState> namedInputs) {
	error.clear();
	auto signals = std::move(pendingActionSignals_);
	pendingActionSignals_.clear();
	QueueActionSignals(frameEvents_, signals);
	if (character_ != 0 &&
	    (interactPressed ||
	     std::ranges::any_of(namedInputs, &GameActionState::pressed))) {
		// 通過経路ではなく現在の接触を使い、床や他の物体の重なりからは操作しない。
		for (const auto &contact : triggers_.GetContacts()) {
			const auto receiver = contact.first == character_ ? contact.second
			                      : contact.second == character_ ? contact.first
			                                                     : 0;
			const auto *object = scene_.FindObject(receiver);
			if (object && SceneActivity::IsActive(scene_, receiver) &&
			    object->collider && object->collider->isTrigger &&
			    object->triggerActions &&
			    !object->triggerActions->actions.empty()) {
				signals.push_back(
				    {receiver, TriggerAction::Event::INTERACT, character_});
			}
		}
	}

	std::set<std::pair<SceneObjectId, std::size_t>> occupied;
	QueueOccupancySignals(signals, occupied);
	auto fired = firedActions_;
	std::map<SceneObjectId, bool> requested;
	std::map<SceneObjectId, SceneMotion::Playback> motionPlayback;
	std::map<SceneObjectId, TriggerAction::Operation> animationPlayback;
	std::map<SceneObjectId, TriggerAction::Operation> animatorPlayback;
	std::vector<SceneObjectId> particleBursts;
	std::vector<ScriptMessage> scriptMessages;
	auto audio = audioActions_;
	std::map<SceneObjectId, SceneObjectId> destinations;
	// 有効な主人公は一体なので、同じ更新では最後に通った設定を使う。
	SceneObjectId checkpoint = 0, checkpointTarget = 0;
	SceneObjectId selectedCamera = 0;
	auto requestedScene = pendingScene_;
	for (const auto &signal : signals) {
		const auto *object = scene_.FindObject(signal.receiver);
		if (!object || !SceneActivity::IsActive(scene_, signal.receiver) ||
		    !object->collider || !object->collider->isTrigger ||
		    !object->triggerActions) {
			continue;
		}
		const auto &actions = object->triggerActions->actions;
		for (std::size_t i = 0; i < actions.size(); ++i) {
			const auto &action = actions[i];
			const auto key = std::pair{signal.receiver, i};
			if ((signal.actionIndex && *signal.actionIndex != i) ||
			    action.event != signal.event ||
			    (action.once && fired.contains(key)) ||
			    (!signal.actionIndex &&
			     !MatchesContact(action, scene_.FindObject(signal.other)))) {
				continue;
			}
			if (action.event == TriggerAction::Event::INTERACT) {
				const bool pressed =
				    action.inputAction.empty()
				        ? interactPressed
				        : std::ranges::any_of(
				              namedInputs, [&](const GameActionState &input) {
					              return input.pressed &&
					                     input.name == action.inputAction;
				              });
				if (!pressed)
					continue;
			}
			const auto *target = scene_.FindObject(action.target);
			if (!action.IsValid() ||
			    (action.operation != TriggerAction::Operation::LOAD_SCENE &&
			     !target)) {
				error = std::format("Trigger Actions on object ID {} have an "
				                    "invalid action or target.",
				                    signal.receiver);
				if (invalidObject) {
					*invalidObject = signal.receiver;
				}
				return false;
			}
			if (action.operation == TriggerAction::Operation::LOAD_SCENE) {
				if (action.scenePath.empty()) {
					error = "Load Scene requires a destination scene asset.";
					return false;
				}
				// 同じ更新に複数の要求があれば、既存の操作順で最後の要求を使用する。
				requestedScene =
				    PathUtils::ResolveFromExecutable(action.scenePath);
				if (action.once)
					fired.insert(key);
				continue;
			}
			if (action.operation == TriggerAction::Operation::SET_CAMERA) {
				selectedCamera = action.target;
				if (action.once)
					fired.insert(key);
				continue;
			}
			if (action.UsesDestination()) {
				const bool settingCheckpoint =
				    action.operation ==
				    TriggerAction::Operation::SET_CHECKPOINT;
				if (settingCheckpoint && (signal.other != action.target ||
				                          action.target != character_))
					continue;
				if (!scene_.Contains(action.destination)) {
					error = "Teleport and Set Checkpoint require an existing "
					        "destination.";
					if (invalidObject) {
						*invalidObject = signal.receiver;
					}
					return false;
				}
				if (settingCheckpoint) {
					checkpoint = action.destination;
					checkpointTarget = action.target;
				} else {
					destinations[action.target] = action.destination;
				}
				if (action.once) {
					fired.insert(key);
				}
				continue;
			}

			if (action.IsMotionPathOperation()) {
				motionPlayback[action.target] =
				    action.operation ==
				            TriggerAction::Operation::PLAY_MOTION_PATH
				        ? SceneMotion::Playback::FORWARD
				    : action.operation ==
				            TriggerAction::Operation::REVERSE_MOTION_PATH
				        ? SceneMotion::Playback::REVERSE
				        : SceneMotion::Playback::PAUSE;
				if (action.once)
					fired.insert(key);
				continue;
			}

			if (action.IsTransformAnimationOperation()) {
				animationPlayback[action.target] = action.operation;
				if (action.once)
					fired.insert(key);
				continue;
			}

			if (action.IsAnimatorOperation()) {
				animatorPlayback[action.target] = action.operation;
				if (action.once)
					fired.insert(key);
				continue;
			}

			if (action.operation == TriggerAction::Operation::EMIT_PARTICLES) {
				particleBursts.push_back(action.target);
				if (action.once)
					fired.insert(key);
				continue;
			}

			if (action.operation ==
			    TriggerAction::Operation::SEND_SCRIPT_MESSAGE) {
				scriptMessages.push_back(
				    {action.target, signal.receiver, action.messageName});
				if (action.once)
					fired.insert(key);
				continue;
			}

			if (action.IsAudioOperation()) {
				if (!target->audioSource) {
					error = std::format("Trigger Actions on object ID {} need "
					                    "an Audio Source on target ID {}.",
					                    signal.receiver, action.target);
					if (invalidObject) {
						*invalidObject = signal.receiver;
					}
					return false;
				}
				audio.push_back(
				    {action.target,
				     action.operation == TriggerAction::Operation::PLAY_AUDIO
				         ? SceneAudioAction::Operation::PLAY
				         : SceneAudioAction::Operation::STOP});
				if (action.once) {
					fired.insert(key);
				}
				continue;
			}
			auto entry =
			    requested.try_emplace(action.target, target->active).first;
			switch (action.operation) {
			case TriggerAction::Operation::ENABLE:
				entry->second = true;
				break;
			case TriggerAction::Operation::DISABLE:
				entry->second = false;
				break;
			case TriggerAction::Operation::TOGGLE:
				entry->second = !entry->second;
				break;
			case TriggerAction::Operation::PLAY_AUDIO:
			case TriggerAction::Operation::STOP_AUDIO:
			case TriggerAction::Operation::LOAD_SCENE:
			case TriggerAction::Operation::TELEPORT:
			case TriggerAction::Operation::SET_CHECKPOINT:
			case TriggerAction::Operation::SET_CAMERA:
			case TriggerAction::Operation::PLAY_MOTION_PATH:
			case TriggerAction::Operation::REVERSE_MOTION_PATH:
			case TriggerAction::Operation::PAUSE_MOTION_PATH:
			case TriggerAction::Operation::PLAY_TRANSFORM_ANIMATION:
			case TriggerAction::Operation::RESTART_TRANSFORM_ANIMATION:
			case TriggerAction::Operation::PAUSE_TRANSFORM_ANIMATION:
			case TriggerAction::Operation::STOP_TRANSFORM_ANIMATION:
			case TriggerAction::Operation::PLAY_ANIMATOR:
			case TriggerAction::Operation::RESTART_ANIMATOR:
			case TriggerAction::Operation::PAUSE_ANIMATOR:
			case TriggerAction::Operation::STOP_ANIMATOR:
			case TriggerAction::Operation::SEND_SCRIPT_MESSAGE:
			case TriggerAction::Operation::EMIT_PARTICLES:
				break;
			}
			if (action.once) {
				fired.insert(key);
			}
		}
	}
	std::vector<SceneActiveChange> changes;
	for (const auto &[id, active] : requested) {
		if (scene_.FindObject(id)->active != active) {
			changes.push_back({id, active});
		}
	}
	if (!changes.empty()) {
		auto events = frameEvents_;
		if (!SetActive(changes, error, invalidObject)) {
			error = "Trigger Actions could not apply their activity changes: " +
			        error;
			return false;
		}
		// 操作が生成した検知は観察用に残し、操作の実行だけを次の正の時間の更新へ送る。
		for (const auto &event : frameEvents_) {
			if (event.type != TriggerEvent::Type::STAY) {
				events.push_back(event);
			}
		}
		frameEvents_.swap(events);
	}
	std::optional<DirectX::XMFLOAT3> checkpointPosition;
	if (checkpoint && character_ == checkpointTarget) {
		SceneWorldTransform destination;
		if (!SceneHierarchy::GetWorldTransform(scene_, checkpoint,
		                                       destination)) {
			error = "Checkpoint destination has an invalid world transform.";
			if (invalidObject)
				*invalidObject = checkpoint;
			return false;
		}
		checkpointPosition = destination.GetPosition();
	}

	if (!destinations.empty()) {
		// 全移動先を変更前のシーンから取得し、親子や相互参照でも順序に依存させない。
		std::map<SceneObjectId, DirectX::XMFLOAT3> positions;
		for (const auto &[target, destination] : destinations) {
			SceneWorldTransform transform;
			if (!SceneHierarchy::GetWorldTransform(scene_, destination,
			                                       transform)) {
				error = "Teleport destination has an invalid world transform.";
				if (invalidObject) {
					*invalidObject = destination;
				}
				return false;
			}
			positions.emplace(target, transform.GetPosition());
		}
		if (!ApplyTeleports(positions, error, invalidObject)) {
			return false;
		}
	}

	if (selectedCamera && !SceneActivity::IsActive(scene_, selectedCamera)) {
		error = "Set Camera requires an active target Camera.";
		if (invalidObject)
			*invalidObject = selectedCamera;
		return false;
	}
	if (checkpointPosition || (selectedCamera && selectedCamera != camera_) ||
	    !animationPlayback.empty()) {
		Scene candidate;
		candidate.CopyFrom(scene_);
		if (checkpointPosition) {
			auto &settings = *candidate.FindObject(checkpointTarget)->respawn;
			settings.useStartPosition = false;
			settings.position = *checkpointPosition;
		}
		// 再生シーンだけのPrimaryを切り替え、追従姿勢や他の実行状態を引き継ぐ。
		if (selectedCamera &&
		    !SceneCameraUtils::SetPrimary(candidate, selectedCamera)) {
			error = "Set Camera requires an active target Camera.";
			if (invalidObject)
				*invalidObject = selectedCamera;
			return false;
		}
		std::vector<SceneObjectId> transformed;
		for (const auto &[id, operation] : animationPlayback) {
			const auto before = candidate.FindObject(id)->transform;
			const auto control =
			    operation == TriggerAction::Operation::STOP_TRANSFORM_ANIMATION
			        ? SceneAnimations::Operation::STOP
			    : operation ==
			            TriggerAction::Operation::PAUSE_TRANSFORM_ANIMATION
			        ? SceneAnimations::Operation::PAUSE
			        : SceneAnimations::Operation::PLAY;
			if (!animations_.Control(
			        candidate, id, control, 0,
			        operation ==
			            TriggerAction::Operation::RESTART_TRANSFORM_ANIMATION,
			        error)) {
				error = "Transform Animation Trigger Action failed: " + error;
				if (invalidObject)
					*invalidObject = id;
				return false;
			}
			if (!TransformUtils::AreEqual(before,
			                              candidate.FindObject(id)->transform))
				transformed.push_back(id);
		}
		if (!SynchronizeSceneChanges(candidate, transformed, error,
		                             invalidObject)) {
			const char *operation =
			    checkpointPosition
			        ? (selectedCamera ? "Set Camera / Set Checkpoint"
			                          : "Set Checkpoint")
			        : "Set Camera";
			error = std::string(animationPlayback.empty() ? operation
			                                              : "Trigger Actions") +
			        " failed: " + error;
			return false;
		}
	}

	for (const auto &[id, playback] : motionPlayback) {
		if (!motion_.SetPlayback(id, playback)) {
			error = "Motion Path Trigger Action target is not prepared.";
			if (invalidObject)
				*invalidObject = id;
			return false;
		}
	}

	for (const auto &[id, operation] : animatorPlayback) {
		const auto control =
		    operation == TriggerAction::Operation::STOP_ANIMATOR
		        ? SceneAnimators::Operation::STOP
		    : operation == TriggerAction::Operation::PAUSE_ANIMATOR
		        ? SceneAnimators::Operation::PAUSE
		        : SceneAnimators::Operation::PLAY;
		if (!animators_.Control(scene_, id, control, {}, 0,
		                        operation ==
		                            TriggerAction::Operation::RESTART_ANIMATOR,
		                        error)) {
			error = "Animator Trigger Action failed: " + error;
			if (invalidObject)
				*invalidObject = id;
			return false;
		}
	}

	// 移動・有効設定・アニメーションの確定後の発生元の位置を使う。
	for (const auto id : particleBursts) {
		const auto *object = scene_.FindObject(id);
		if (!object || !object->particleEmitter) {
			error = "Emit Particles requires a target with a Particle Emitter.";
			if (invalidObject)
				*invalidObject = id;
			return false;
		}
		if (!particles_.Control(scene_, id, SceneParticles::Operation::EMIT,
		                        object->particleEmitter->burst, false, error)) {
			error = "Emit Particles failed: " + error;
			if (invalidObject)
				*invalidObject = id;
			return false;
		}
	}

	for (const auto &message : scriptMessages) {
		if (!scripts_.QueueTriggerMessage(scene_, message.receiver,
		                                  message.sender, message.name,
		                                  error)) {
			error = "Send Script Message failed: " + error;
			if (invalidObject)
				*invalidObject = message.sender;
			return false;
		}
	}

	pendingScene_.swap(requestedScene);
	firedActions_.swap(fired);
	occupiedActions_.swap(occupied);
	audioActions_.swap(audio);
	return true;
}

bool SceneSimulation::ApplyTeleports(
    const std::map<SceneObjectId, DirectX::XMFLOAT3> &positions,
    std::string &error, SceneObjectId *invalidObject) {
	Scene working;
	working.CopyFrom(scene_);
	if (!SceneHierarchy::SetWorldPositions(working, positions)) {
		error = "Teleport positions cannot be represented in this hierarchy.";
		return false;
	}

	std::vector<SceneObjectId> transformed;
	for (const auto &[id, position] : positions) {
		(void)position;
		transformed.push_back(id);
	}
	return SynchronizeSceneChanges(working, transformed, error, invalidObject);
}

bool SceneSimulation::SynchronizeSceneChanges(
    const Scene &working, std::span<const SceneObjectId> transformed,
    std::string &error, SceneObjectId *invalidObject,
    std::vector<TriggerEvent> *generated) {
	SceneSimulation next;
	next.CopyFrom(*this);
	// 追従対象またはカメラの親が移動した場合は、補間せず到着先の姿勢へ同期する。
	const auto moved = [&](SceneObjectId id) {
		SceneWorldTransform before, after;
		return !SceneHierarchy::GetWorldTransform(scene_, id, before) ||
		       !SceneHierarchy::GetWorldTransform(working, id, after) ||
		       std::memcmp(&before.matrix, &after.matrix,
		                   sizeof(before.matrix)) != 0;
	};
	std::set<SceneObjectId> followMoved;
	for (const auto id : updateOrder_) {
		if (!working.Contains(id))
			continue;
		const auto &object = *working.FindObject(id);
		if (object.followCamera && SceneActivity::IsActive(working, id) &&
		    (moved(id) || moved(object.followCamera->target) ||
		     followMoved.contains(id) ||
		     followMoved.contains(object.followCamera->target))) {
			next.followPoses_.erase(id);
			std::vector<SceneObjectId> subtree;
			if (!SceneHierarchy::GetSubtree(working, id, subtree)) {
				error = "Teleport Follow Camera hierarchy is invalid.";
				return false;
			}
			followMoved.insert(subtree.begin(), subtree.end());
		}
	}
	if (!next.Configure(working, error, fallbackPose_, invalidObject, true)) {
		return false;
	}

	// 削除で詰めたTrigger Actionの添字へ、一度だけ実行済みの状態を引き継ぐ。
	std::set<std::pair<SceneObjectId, std::size_t>> fired, occupied;
	for (const auto &[id, before] : scene_.GetObjects()) {
		const auto *after = working.FindObject(id);
		if (!after || !before.triggerActions || !after->triggerActions)
			continue;
		std::size_t current = 0;
		for (std::size_t previous = 0;
		     previous < before.triggerActions->actions.size() &&
		     current < after->triggerActions->actions.size();
		     ++previous) {
			if (before.triggerActions->actions[previous] !=
			    after->triggerActions->actions[current])
				continue;
			if (firedActions_.contains({id, previous}))
				fired.emplace(id, current);
			if (occupiedActions_.contains({id, previous}))
				occupied.emplace(id, current);
			++current;
		}
	}
	next.firedActions_ = std::move(fired);
	next.occupiedActions_ = std::move(occupied);
	std::erase_if(next.audioActions_, [&](const auto &action) {
		return !working.Contains(action.target);
	});
	std::erase_if(next.pendingActionSignals_, [&](const auto &signal) {
		return !working.Contains(signal.receiver);
	});
	bool resetCharacter = false;
	std::set<SceneObjectId> teleported;
	for (const auto id : transformed) {
		std::vector<SceneObjectId> subtree;
		if (!SceneHierarchy::GetSubtree(scene_, id, subtree)) {
			error = "Teleport hierarchy is invalid.";
			return false;
		}
		teleported.insert(subtree.begin(), subtree.end());
		resetCharacter |= std::find(subtree.begin(), subtree.end(),
		                            character_) != subtree.end();
	}
	// 追従によって移動したColliderにも途中経路の検知を適用しない。
	for (const auto &[id, object] : next.scene_.GetObjects()) {
		(void)object;
		SceneWorldTransform before, after;
		if (SceneHierarchy::GetWorldTransform(scene_, id, before) &&
		    SceneHierarchy::GetWorldTransform(next.scene_, id, after) &&
		    (std::memcmp(&before.matrix, &after.matrix,
		                 sizeof(before.matrix)) != 0)) {
			teleported.insert(id);
		}
	}
	resetCharacter |= teleported.contains(character_);
	if (next.character_ && resetCharacter &&
	    !next.controller_.Teleport(next.controller_.GetTransform().translation,
	                               next.world_)) {
		error = "Teleport character destination overlaps a solid collider.";
		if (invalidObject) {
			*invalidObject = character_;
		}
		return false;
	}

	const std::vector<SceneObjectId> ids(teleported.begin(), teleported.end());
	auto triggers = triggers_;
	if (!triggers.Update(next.scene_, ids)) {
		error = "Teleport trigger shapes are invalid.";
		return false;
	}
	next.triggers_ = std::move(triggers);
	const auto events = next.triggers_.GetEvents();
	next.QueueActionSignals(events, next.pendingActionSignals_);
	for (const auto &event : events) {
		if (event.type != TriggerEvent::Type::STAY) {
			next.frameEvents_.push_back(event);
			if (generated)
				generated->push_back(event);
		}
	}
	Adopt(next);
	return true;
}
