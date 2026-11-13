#include "gameplay/SceneSimulation.h"
#include "animation/AnimationClipAsset.h"
#include "gameplay/SceneCameraUtils.h"
#include "gameplay/SceneValidation.h"
#include "graphics/camera/Camera.h"
#include "math/TransformUtils.h"
#include "physics/ColliderUtils.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneLighting.h"
#include "scene/SceneSerializer.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <limits>
#include <map>
#include <set>
#include <string_view>

namespace {
/// <summary>
/// 遷移先の編集データを読み込んだ候補で、主人公をタグ付き物体の位置へ移す
/// </summary>
/// <param name="scene">読み込んだ直後の遷移先</param>
/// <param name="tag">到着地点のタグ。空は元の配置を使う</param>
/// <param name="error">失敗理由</param>
/// <returns>true:準備成功 false:タグや主人公を一意に解決できない、または位置が無効</returns>
bool PlaceSceneArrival(Scene &scene, std::string_view tag, std::string &error) {
	if (tag.empty())
		return true;
	SceneObjectId arrival = 0, character = 0;

	for (const auto &[id, object] : scene.GetObjects()) {
		if (!SceneActivity::IsActive(scene, id))
			continue;
		if (object.tag == tag) {
			if (arrival) {
				error = std::format(
				    "Arrival Tag '{}' matches multiple active objects.", tag);
				return false;
			}

			arrival = id;
		}

		if (object.character) {
			if (character) {
				error = "A tagged arrival requires exactly one active "
				        "Character Controller.";
				return false;
			}

			character = id;
		}
	}

	if (!arrival) {
		error = std::format(
		    "Arrival Tag '{}' has no active object in the destination scene.",
		    tag);
		return false;
	}

	if (!character) {
		error = "A tagged arrival requires an active Character Controller.";
		return false;
	}

	SceneWorldTransform world;

	if (!SceneHierarchy::GetWorldTransform(scene, arrival, world) ||
	    !SceneHierarchy::SetWorldPosition(scene, character,
	                                      world.GetPosition())) {
		error = "The destination Character Controller cannot be placed at the "
		        "Arrival Tag.";
		return false;
	}

	return true;
}
} // namespace

bool SceneSimulation::Start(const Scene &source, std::string &error,
                            const CameraPose &fallbackPose,
                            SceneObjectId *invalidObject,
                            std::string_view initialScriptData) {
	return StartScene(source, error, fallbackPose, invalidObject, false,
	                  initialScriptData);
}

bool SceneSimulation::RestartCurrentScene(std::string &error,
                                          std::filesystem::path *failedScript) {
	if (failedScript) {
		failedScript->clear();
	}
	if (!IsPlaying() || !restartSource_) {
		error = "Start Play before restarting the scene.";
		return false;
	}

	SceneObjectId invalidObject = 0;
	if (StartScene(*restartSource_, error, fallbackPose_, &invalidObject, true)) {
		return true;
	}
	if (failedScript) {
		*failedScript = SceneScripts::FindFailedScript(*restartSource_, invalidObject, error);
	}
	return false;
}

bool SceneSimulation::Validate(const Scene &source, std::string &error,
                               const CameraPose &fallbackPose,
                               SceneObjectId *invalidObject) {
	if (invalidObject)
		*invalidObject = 0;
	try {
		Scene runtime;
		runtime.CopyFrom(source);

		for (const auto &[id, value] : runtime.GetObjects()) {
			(void)value;
			runtime.FindObject(id)->prefabLink.reset();
		}

		SceneSimulation check;
		return check.Configure(runtime, error, fallbackPose, invalidObject,
		                       false);
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool SceneSimulation::ValidateArrival(const Scene &source, std::string_view tag,
                                      std::string &error,
                                      const CameraPose &fallbackPose) {
	Scene candidate;
	candidate.CopyFrom(source);
	return PlaceSceneArrival(candidate, tag, error) &&
	       Validate(candidate, error, fallbackPose);
}

bool SceneSimulation::StartScene(const Scene &source, std::string &error,
                                 const CameraPose &fallbackPose,
                                 SceneObjectId *invalidObject,
                                 bool preserveSharedData,
                                 std::string_view initialScriptData) {
	if (invalidObject)
		*invalidObject = 0;
	try {
		if (sceneRevision_ == (std::numeric_limits<std::uint64_t>::max)()) {
			error = "The scene revision limit was reached.";
			return false;
		}

		if (audioRevision_ == (std::numeric_limits<std::uint64_t>::max)()) {
			error = "The simulation audio revision limit was reached.";
			return false;
		}

		auto initialScene = restartSource_;

		if (&source != initialScene.get()) {
			auto snapshot = std::make_shared<Scene>();
			auto objects = source.GetObjects();

			for (auto &[id, object] : objects) {
				(void)id;
				object.prefabLink.reset();
			}

			snapshot->CopyFrom(source);
			snapshot->ReplaceObjects(std::move(objects));
			SceneLighting::MigrateLegacyDirectionalLight(*snapshot, legacyLighting_);
			initialScene = std::move(snapshot);
		}

		SceneSimulation working;
		working.restartSource_ = initialScene;
		if (preserveSharedData) {
			working.loadedScenePath_ = loadedScenePath_;
		}
		working.preparePrefabResources_ = preparePrefabResources_;
		working.loadAnimatorModel_ = loadAnimatorModel_;
		working.legacyLighting_ = legacyLighting_;
		working.sharedScriptData_ =
		    preserveSharedData ? std::string_view(sharedScriptData_)
		                       : initialScriptData;
		if (!working.Configure(*initialScene, error, fallbackPose,
		                       invalidObject, false)) {
			return false;
		}

		std::vector<SceneObjectId> autoplay;
		for (const auto &[id, object] : working.scene_.GetObjects()) {
			if (object.animationClip && object.animationClip->playOnStart &&
			    SceneActivity::IsActive(working.scene_, id))
				autoplay.push_back(id);
		}

		working.pendingActionSignals_.clear();
		for (const auto id : autoplay) {
			if (!working.PlayAnimationClip(id, error)) {
				if (invalidObject)
					*invalidObject = id;
				return false;
			}
		}

		if (!working.ApplyScripts(0, CharacterInput{}, true, error,
		                          invalidObject))
			return false;

		if (!working.triggers_.Reset(working.scene_)) {
			error = "Initial Script trigger shapes are invalid.";
			return false;
		}

		const auto events = working.triggers_.GetEvents();
		working.frameEvents_.assign(events.begin(), events.end());

		if (!working.ApplyTriggerActions(error, invalidObject)) {
			return false;
		}

		working.scripts_.QueueTriggers(working.frameEvents_);
		working.audioRevision_ = audioRevision_ + 1;
		working.sceneRevision_ = sceneRevision_ + 1;
		Adopt(working);
		scripts_.FlushLogs();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
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
		working.CopyFrom(scene_);
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

		if (!changed) {
			return true;
		}

		SceneSimulation next;
		next.CopyFrom(*this);

		if (!next.Configure(working, error, next.fallbackPose_, invalidObject,
		                    true)) {
			return false;
		}

		const auto events = next.triggers_.GetEvents();
		next.frameEvents_.assign(events.begin(), events.end());
		next.QueueActionSignals(events, next.pendingActionSignals_);
		Adopt(next);
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool SceneSimulation::Configure(const Scene &original, std::string &error,
                                const CameraPose &fallbackPose,
                                SceneObjectId *invalidObject, bool preserve,
                                bool deferTriggerUpdate) {
	if (invalidObject) {
		*invalidObject = 0;
	}

	try {
		error.clear();
		Scene working;
		working.CopyFrom(original);
		auto animations = animations_;
		if (!animations.Configure(working, preserve, error, invalidObject))
			return false;

		const auto fail = [&](SceneObjectId id, std::string_view reason) {
			const auto *object = working.FindObject(id);
			error = std::format(
			    "{} Object: \"{}\" (ID {}).", reason,
			    object && !object->name.empty() ? object->name : "Object", id);
			if (invalidObject) {
				*invalidObject = id;
			}

			return false;
		};
		std::vector<SceneObjectId> order;

		if (!SceneCameraUtils::GetUpdateOrder(working, order, error,
		                                      invalidObject)) {
			return false;
		}

		std::map<SceneObjectId, FollowCameraOrbit> orbits;

		for (const auto id : order) {
			const auto &object = *working.FindObject(id);

			if (!object.followCamera || !object.followCamera->orbit.enabled) {
				continue;
			}

			const auto found = orbitStates_.find(id);

			if (preserve && found != orbitStates_.end()) {
				orbits.emplace(id, found->second);
			} else if (SceneActivity::IsActive(working, id) &&
			           !orbits[id].Initialize(*object.followCamera)) {
				return fail(id, "Orbit Camera settings are invalid.");
			}
		}

		const auto makeFollow = [&](SceneObjectId id,
		                            const DirectX::XMFLOAT3 &target,
		                            const FollowCameraSettings &settings) {
			const auto orbit = orbits.find(id);
			return orbit != orbits.end()
			           ? orbit->second.MakePose(target, settings)
			           : SceneCameraUtils::MakeFollowPose(target, settings);
		};

		if (!preserve) {
			Scene resolved;
			if (!SceneCameraUtils::ResolveInitialScene(working, resolved, error,
			                                           invalidObject)) {
				return false;
			}
			working.AdoptValidated(resolved);
		} else {
			// 継続する追従は現在姿勢を保ち、新しく有効になった枝だけを依存順で初期化する。
			SceneCameraUtils::FollowObstacleCache obstacles(working);
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

				auto pose =
				    makeFollow(id, target.GetPosition(), *object.followCamera);
				if (!obstacles.AvoidObstacles(id, pose) ||
				    !SceneCameraUtils::ApplyWorldPose(working, id, pose)) {
					return fail(
					    id, "The newly active Follow Camera pose is invalid.");
				}
				obstacles.TransformChanged(id);
			}
		}

		for (const auto &[id, object] : working.GetObjects()) {
			if (object.animationClip && object.animationClip->usesAsset) {
				AnimationClipComponent resolved;
				std::string issue;
				if (!AnimationClipAsset::Resolve(*object.animationClip, resolved, issue))
					return fail(id, issue);
				working.FindObject(id)->animationClip = std::move(resolved);
			}
		}

		// 現在の姿勢を検証し、既存の追従や移動を開始時へ巻き戻さない。
		const auto issues =
		    SceneValidation::Inspect(working, fallbackPose, false,
		                             preserve ? &world_ : nullptr);
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

		auto physics = preserve ? physics_ : ScenePhysics{};
		if (!physics.Configure(working, preserve, error, invalidObject))
			return false;
		auto world = preserve ? world_ : CollisionWorld{};
		SceneObjectId invalidCollider = 0;

		if (!world.Rebuild(working, std::span<const SceneObjectId>{ignored},
		                   &invalidCollider, physics.GetWorld())) {
			return fail(
			    invalidCollider,
			    "Collider cannot be represented at its world transform.");
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
			    (continuing ? (!controller.SetSettings(*object.character) ||
			                   !controller.SynchronizeWorldMatrix(
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
		SceneCameraUtils::FollowObstacleCache initialObstacles(working);

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

			auto pose =
			    makeFollow(id, target.GetPosition(), *object.followCamera);
			if (!initialObstacles.AvoidObstacles(id, pose)) {
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
		if (!deferTriggerUpdate) {
			if (preserve)
				triggers = triggers_;
			if (preserve ? !triggers.Update(working) : !triggers.Reset(working))
				return fail(0, "Trigger world shapes or hierarchy are invalid.");
		}

		auto particles = particles_;
		if (!particles.Configure(working, preserve, error, invalidObject))
			return false;
		auto motion = motion_;
		if (!motion.Configure(working, preserve))
			return fail(0, "Motion Path settings are invalid.");
		auto scripts = scripts_;
		if (!scripts.Configure(working, preserve, error, invalidObject))
			return false;
		auto ui = preserve ? ui_ : SceneUI{};
		if (!ui.Refresh(working, error))
			return false;
		if (preparePrefabResources_ &&
		    !preparePrefabResources_(scripts.GetPrefabScenes(), error))
			return false;
		auto animators = animators_;
		if (!animators.Configure(working, preserve, error, invalidObject,
		                         loadAnimatorModel_, scripts.GetPrefabScenes()))
			return false;
		// 全候補が有効な場合だけ確定し、失敗時は再生シーンと実行状態をすべて保持する。
		scene_.AdoptValidated(working);
		world_ = std::move(world);
		if (!deferTriggerUpdate)
			triggers_ = std::move(triggers);
		controller_ = controller;
		motion_ = std::move(motion);
		animations_ = std::move(animations);
		particles_ = std::move(particles);
		animators_ = std::move(animators);
		physics_ = std::move(physics);
		scripts_ = std::move(scripts);
		ui_ = std::move(ui);
		character_ = character;
		if (!preserve || camera != camera_)
			cameraTransition_.reset();
		camera_ = camera;
		updateOrder_ = std::move(order);
		followPoses_ = std::move(follows);
		orbitStates_ = std::move(orbits);
		cameraSettings_ = lens;
		spawn_ = spawn;

		if (!continuing) {
			respawnCount_ = 0;
			characterAirflow_ = {};
			lastRespawnPosition_.reset();
		}

		if (!cameraTransition_)
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
	sharedScriptData_ = "{}";
	playing_ = false;
	pendingScene_ = {};
	failedDestination_.reset();
	restartSource_.reset();
	loadedScenePath_.reset();
	quitRequested_ = false;
	gamePaused_ = false;
	cameraTransition_.reset();
	motion_ = SceneMotion{};
	animations_ = SceneAnimations{};
	animationClips_.clear();
	particles_ = SceneParticles{};
	animators_ = SceneAnimators{};
	physics_ = ScenePhysics{};
	scripts_ = SceneScripts{};
	ui_ = SceneUI{};
	frameEvents_.clear();
	audioActions_.clear();
	pendingActionSignals_.clear();
	delayedActions_.clear();
	firedActions_.clear();
	occupiedActions_.clear();
	lastUpdateError_.clear();
	triggers_.Clear();
	collisions_ = CollisionEvents{};
	scene_.Clear();
	character_ = camera_ = 0;
	characterAirflow_ = {};
	updateOrder_.clear();
	followPoses_.clear();
	orbitStates_.clear();
	respawnCount_ = 0;
	lastRespawnPosition_.reset();
}

bool SceneSimulation::Update(float seconds, const CharacterInput &input,
                             const OrbitCameraInput &cameraInput,
                             const UIInput &uiInput,
                             std::span<const GameActionState> actions,
                             SceneObjectId *invalidObject) {
	failedDestination_.reset();
	if (invalidObject) {
		*invalidObject = 0;
	}

	if (!playing_ || !std::isfinite(seconds) || seconds < 0 ||
	    !cameraInput.IsValid() || !uiInput.IsValid() ||
	    !GameActionState::IsValid(actions)) {
		lastUpdateError_ =
		    "Start Play and use a finite, non-negative simulation time.";
		return false;
	}
	try {
		if (quitRequested_)
			return true;
		if (seconds == 0) {
			if (!gamePaused_ && !Advance(seconds, input, cameraInput)) {
				lastUpdateError_ = "The zero-time simulation update failed.";
				return false;
			}

			ui_.Cancel();
			lastUpdateError_.clear();
			return true;
		}

		if (pendingScene_.restart || !pendingScene_.path.empty()) {
			Scene destination;
			std::string error;

			if (!pendingScene_.restart) {
				if (!SceneSerializer::Load(destination, pendingScene_.path)) {
					lastUpdateError_ = "Could not load destination scene: " +
					                   PathUtils::ToUtf8(pendingScene_.path);
					return false;
				}

				failedDestination_.emplace();
				if (!PlaceSceneArrival(destination, pendingScene_.arrivalTag,
				                       error)) {
					lastUpdateError_ = std::move(error);
					return false;
				}
			}

			const auto &source =
			    pendingScene_.restart ? *restartSource_ : destination;
			SceneSimulation destinationSimulation;
			destinationSimulation.CopyFrom(*this);
			if (!pendingScene_.restart) {
				destinationSimulation.loadedScenePath_ =
				    std::make_shared<const std::filesystem::path>(pendingScene_.path);
			}

			SceneObjectId destinationObject = 0;
			if (!destinationSimulation.StartScene(source, error, fallbackPose_,
			                                      &destinationObject, true)) {
				if (failedDestination_ &&
				    destination.Contains(destinationObject)) {
					failedDestination_->objectId = destinationObject;
					failedDestination_->scriptPath =
					    SceneScripts::FindFailedScript(destination, destinationObject,
					                                   error);
				}
				lastUpdateError_ = std::move(error);
				return false;
			}

			Adopt(destinationSimulation);
			return true;
		}

		SceneSimulation working;
		working.CopyFrom(*this);
		working.audioActions_.clear();

		if (audioRevision_ == (std::numeric_limits<std::uint64_t>::max)()) {
			lastUpdateError_ =
			    "The simulation audio revision limit was reached.";
			return false;
		}

		std::string uiError;

		if (!working.ui_.Update(working.scene_, uiInput, uiError)) {
			lastUpdateError_ = std::move(uiError);
			return false;
		}

		const bool paused = working.gamePaused_;
		const auto controls = paused || working.ui_.ConsumesNavigation()
		                          ? CharacterInput{}
		                          : input;
		const auto orbit =
		    (working.ui_.ConsumesNavigation() || working.ui_.ConsumesPointer())
		        ? OrbitCameraInput{}
		        : cameraInput;
		if (paused) {
			working.frameEvents_.clear();
			working.collisions_.ClearEvents();
		} else {
			if (!working.Advance(seconds, controls, orbit)) {
				lastUpdateError_ =
				    working.lastUpdateError_.empty()
				        ? "Simulation update failed. Check active "
				          "physics and camera settings."
				        : working.lastUpdateError_;
				return false;
			}

			// 主人公や追従の最終姿勢で問い合わせる。実行中のKinematic速度は変えない。
			auto contactPhysics = working.physics_;
			std::string contactError;

			if (!contactPhysics.Configure(working.scene_, true, contactError)) {
				lastUpdateError_ = std::move(contactError);
				return false;
			}

			const auto *physicalWorld = working.physics_.GetWorld();
			const auto impacts = physicalWorld && seconds >= .000001f
			                         ? physicalWorld->GetFrameContacts()
			                         : std::span<const CollisionContact>{};
			if (!working.collisions_.Update(
			        working.scene_, contactPhysics.GetWorld(), impacts)) {
				lastUpdateError_ = "Collision notification shapes or contact "
				                   "count are invalid.";
				return false;
			}
		}

		std::string error;
		const bool uiConsumesInput =
		    working.ui_.ConsumesNavigation() || working.ui_.ConsumesPointer();
		if (!working.ApplyTriggerActions(error, nullptr,
		                                 controls.interactPressed, actions,
		                                 seconds, uiConsumesInput)) {
			lastUpdateError_ = std::move(error);
			return false;
		}

		std::vector<GameActionState> scriptActions(actions.begin(),
		                                           actions.end());
		if (working.ui_.ConsumesNavigation() || working.ui_.ConsumesPointer()) {
			for (auto &action : scriptActions) {
				action.down = action.pressed = action.released = false;
				action.value = 0;
			}
		}

		auto scriptViewport = uiInput;
		// UIがポインターを使用した更新では、同じクリックを3D操作へ渡さない。
		scriptViewport.pointerValid &= !working.ui_.ConsumesPointer();
		SceneObjectId scriptObject = 0;
		if (!working.ApplyScripts(paused ? 0.f : seconds, controls, false,
		                          error, &scriptObject, scriptActions,
		                          &scriptViewport)) {
			if (invalidObject) {
				*invalidObject = scriptObject;
			}
			lastUpdateError_ = std::move(error);
			return false;
		}
		if (!working.ui_.Refresh(working.scene_, error)) {
			lastUpdateError_ = std::move(error);
			return false;
		}

		working.lastUpdateError_.clear();
		working.audioRevision_ = audioRevision_ + 1;
		Adopt(working);
		scripts_.FlushLogs();
		return true;
	} catch (const std::exception &exception) {
		lastUpdateError_ = exception.what();
		return false;
	}
}

bool SceneSimulation::Advance(float seconds, const CharacterInput &input,
                              const OrbitCameraInput &cameraInput) {
	if (!playing_ || !std::isfinite(seconds) || seconds < 0)
		return false;
	lastUpdateError_.clear();

	try {
		Scene working;
		working.CopyFrom(scene_);
		auto next = controller_;
		auto animations = animations_;
		std::string animationError;

		if (!animations.Advance(working, seconds, animationError)) {
			lastUpdateError_ = std::move(animationError);
			return false;
		}

		auto clips = animationClips_;
		auto clipSignals = pendingActionSignals_;
		if (!AdvanceAnimationClips(working, seconds, clips, clipSignals,
		                           lastUpdateError_))
			return false;

		auto motion = motion_;
		std::optional<TriggerWorld> triggers;
		std::vector<TriggerEvent> motionEvents;
		if (seconds > 0)
			triggers = triggers_;
		if (!motion.Advance(working, seconds, character_ ? &next : nullptr,
		                    character_, triggers ? &*triggers : nullptr,
		                    triggers ? &motionEvents : nullptr))
			return false;
		auto physics = physics_;
		std::string physicsError;

		if (!physics.Advance(working, seconds, physicsError)) {
			lastUpdateError_ = std::move(physicsError);
			return false;
		}

		auto follows = followPoses_;
		auto orbits = orbitStates_;
		if (const auto found = orbits.find(camera_);
		    found != orbits.end() &&
		    !found->second.Update(seconds, cameraInput,
		                          *working.FindObject(camera_)->followCamera)) {
			return false;
		}

		auto world = character_ ? world_ : CollisionWorld{};
		std::vector<SceneObjectId> ignored;
		if (character_ &&
		    !SceneHierarchy::GetSubtree(working, character_, ignored))
			return false;
		bool respawned = false;
		SceneCameraUtils::FollowObstacleCache cameraObstacles(working);
		// 親と追従対象を先に更新する。カメラを親にした主人公も同じ依存順で処理する。
		for (const auto id : updateOrder_) {
			const auto *object = working.FindObject(id);
			if (!object)
				return false;
			if (!SceneActivity::IsActive(working, id))
				continue;
			// 親の追従・移動を反映した後も、動的な剛体のワールド姿勢を保持する。
			if (object->rigidBody && object->rigidBody->enabled) {
				const auto *physicalWorld = physics.GetWorld();
				const auto state =
				    physicalWorld ? physicalWorld->GetState(id) : std::nullopt;
				if (!state || !SceneHierarchy::SetWorldPose(
				                  working, id, state->transform.translation,
				                  state->transform.rotation))
					return false;
				cameraObstacles.TransformChanged(id);
			}

			if (id == character_) {
				SceneWorldTransform transform;
				if (!object->collider || !object->character ||
				    !world.Rebuild(working,
				                   std::span<const SceneObjectId>{ignored},
				                   nullptr, physics.GetWorld()) ||
				    !SceneHierarchy::GetWorldTransform(working, id,
				                                       transform) ||
				    !next.SynchronizeWorldMatrix(transform.matrix, world) ||
				    !next.Update(seconds, input, world,
				                 {characterAirflow_[0], characterAirflow_[1],
				                  characterAirflow_[2]}))
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
				cameraObstacles.TransformChanged(id);
			}

			if (object->followCamera) {
				const auto &settings = *object->followCamera;
				const auto found = follows.find(id);
				SceneWorldTransform target;
				if (!object->camera || found == follows.end() ||
				    !SceneHierarchy::GetWorldTransform(working, settings.target,
				                                       target))
					return false;
				const auto orbit = orbits.find(id);
				const auto desired =
				    orbit != orbits.end()
				        ? orbit->second.MakePose(target.GetPosition(), settings)
				        : SceneCameraUtils::MakeFollowPose(target.GetPosition(),
				                                           settings);
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
				lerp(pose.target, desired.target);

				if (orbit != orbits.end()) {
					// 注視点の追従だけを補間し、旋回中に半径が縮んで対象を横切ることを避ける。
					pose.position = {
					    pose.target.x + desired.position.x - desired.target.x,
					    pose.target.y + desired.position.y - desired.target.y,
					    pose.target.z + desired.position.z - desired.target.z};
				} else {
					lerp(pose.position, desired.position);
				}

				if (!cameraObstacles.AvoidObstacles(id, pose)) {
					pose.position = previous.position;
					Camera validator;
					if (!validator.SetPose(pose))
						pose = previous;
				}

				if (!SceneCameraUtils::ApplyWorldPose(working, id, pose))
					return false;
				cameraObstacles.TransformChanged(id);
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

		auto transition = cameraTransition_;

		if (transition && seconds == 0) {
			pose = pose_;
		} else if (transition) {
			transition->elapsed += seconds;

			if (transition->elapsed >= transition->duration) {
				transition.reset();
			} else {
				const auto t = static_cast<float>(transition->elapsed /
				                                  transition->duration);
				const auto blend = t * t * (3.f - 2.f * t);
				if (!SceneCameraUtils::BlendPose(transition->from, pose, blend,
				                                 pose))
					return false;
			}
		}

		// 全更新の成功後に確定し、失敗したフレームは位置・速度・追従状態を保持する。
		if (!world.Rebuild(working, std::span<const SceneObjectId>{ignored},
		                   nullptr, physics.GetWorld()))
			return false;
		if (character_) {
			SceneWorldTransform transform;
			if (!SceneHierarchy::GetWorldTransform(working, character_,
			                                       transform) ||
			    !next.SynchronizeWorldMatrix(transform.matrix, world))
				return false;
		}

		// 落下復帰では主人公と子孫の瞬間移動経路を検知しない。
		// 終点でのEnterと、開始時の接触に対するExitは通常どおり生成する。
		if (seconds > 0) {
			if (!triggers->Update(
			        working, respawned ? std::span<const SceneObjectId>{ignored}
			                           : std::span<const SceneObjectId>{})) {
				return false;
			}
		}

		auto particles = particles_;
		if (!particles.Advance(working, seconds, lastUpdateError_))
			return false;
		auto animators = animators_;
		if (!animators.Advance(working, seconds, lastUpdateError_))
			return false;

		if (triggers) {
			for (const auto &event : triggers->GetEvents()) {
				if (event.type == TriggerEvent::Type::STAY &&
				    std::ranges::any_of(motionEvents, [&](const auto &prior) {
					    return prior.contact == event.contact;
				    }))
					continue;
				motionEvents.push_back(event);
			}

			frameEvents_ = std::move(motionEvents);
			triggers_ = std::move(*triggers);
		}

		scene_.AdoptValidated(working);
		controller_ = next;
		motion_ = std::move(motion);
		animations_ = std::move(animations);
		animationClips_ = std::move(clips);
		pendingActionSignals_.swap(clipSignals);
		particles_ = std::move(particles);
		animators_ = std::move(animators);
		physics_ = std::move(physics);
		world_ = std::move(world);
		followPoses_ = std::move(follows);
		orbitStates_ = std::move(orbits);
		pose_ = pose;
		cameraTransition_ = transition;

		if (respawned) {
			++respawnCount_;
			lastRespawnPosition_ = next.GetTransform().translation;
			characterAirflow_ = {};
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

std::optional<RigidBodyState>
SceneSimulation::GetRigidBodyState(SceneObjectId id) const {
	const auto *object = scene_.FindObject(id);
	const auto *world = physics_.GetWorld();
	return playing_ && world && object && object->rigidBody &&
	               object->rigidBody->enabled &&
	               SceneActivity::IsActive(scene_, id)
	           ? world->GetState(id)
	           : std::nullopt;
}

namespace {
/// <summary>
/// キーから求めた値を再生シーンのローカル座標・UIへ適用する
/// </summary>
bool ApplyAnimationClip(Scene &scene, const AnimationClipComponent &clip,
                        double seconds, std::string &error) {
	for (const auto &track : clip.tracks) {
		auto *target = scene.FindObject(track.target);
		if (!target || !target->CanAnimate(track)) {
			error = "Animation Clip target is missing a required component or "
			        "has another transform controller.";
			return false;
		}

		const auto value = track.Sample(seconds);
		switch (track.property) {
		case AnimationClipTrack::Property::POSITION:
			target->rectTransform->position = {value[0], value[1]};
			break;
		case AnimationClipTrack::Property::SIZE_DELTA:
			target->rectTransform->sizeDelta = {value[0], value[1]};
			break;
		case AnimationClipTrack::Property::UI_ROTATION:
			target->rectTransform->rotationDegrees = value[0];
			break;
		case AnimationClipTrack::Property::OPACITY:
			if (target->text)
				target->text->color[3] = value[0];
			if (target->image)
				target->image->color[3] = value[0];
			break;
		case AnimationClipTrack::Property::TRANSLATION:
			target->transform.translation = {value[0], value[1], value[2]};
			break;
		case AnimationClipTrack::Property::ROTATION:
			target->transform.rotation = {value[0], value[1], value[2], value[3]};
			break;
		case AnimationClipTrack::Property::SCALE:
			target->transform.scale = {value[0], value[1], value[2]};
			break;
		}
	}

	return true;
}
} // namespace

void SceneSimulation::QueueClipMarkers(const SceneObject &object, SceneObjectId id,
                                       double previous, double current,
                                       std::vector<ActionSignal> &signals) {
	if (!object.triggerActions)
		return;

	const auto &actions = object.triggerActions->actions;
	std::vector<std::size_t> markers;
	for (std::size_t i = 0; i < actions.size(); ++i) {
		const auto &action = actions[i];
		if (action.event == TriggerAction::Event::ANIMATION_CLIP_MARKER &&
		    action.clipTime > previous && action.clipTime <= current)
			markers.push_back(i);
	}

	std::stable_sort(markers.begin(), markers.end(), [&](auto a, auto b) {
		return actions[a].clipTime < actions[b].clipTime;
	});
	for (const auto index : markers)
		signals.push_back({id, TriggerAction::Event::ANIMATION_CLIP_MARKER, 0, index});
}

bool SceneSimulation::PlayAnimationClip(SceneObjectId id, std::string &error,
                                         bool queueStartMarkers) {
	const auto *object = scene_.FindObject(id);
	if (!object || !object->animationClip || !object->animationClip->IsValid() ||
	    !SceneActivity::IsActive(scene_, id)) {
		error = "Play Animation Clip requires an active target Animation Clip.";
		return false;
	}

	if (std::ranges::any_of(object->animationClip->tracks,
	                       &AnimationClipTrack::IsTransform)) {
		Scene candidate;
		candidate.CopyFrom(scene_);
		if (!ApplyAnimationClip(candidate, *object->animationClip, 0, error) ||
		    !SynchronizeSceneChanges(candidate, {}, error, nullptr))
			return false;
	} else if (!ApplyAnimationClip(scene_, *object->animationClip, 0, error)) {
		return false;
	}

	animationClips_[id] = {};
	if (queueStartMarkers)
		QueueClipMarkers(*scene_.FindObject(id), id, -1, 0,
		                 pendingActionSignals_);
	return true;
}

bool SceneSimulation::AdvanceAnimationClips(
    Scene &scene, float seconds, std::map<SceneObjectId, AnimationClipPlayback> &clips,
    std::vector<ActionSignal> &signals, std::string &error) {
	if (seconds <= 0)
		return true;

	for (auto it = clips.begin(); it != clips.end();) {
		const auto id = it->first;
		const auto *object = scene.FindObject(id);
		if (!object || !object->animationClip || !SceneActivity::IsActive(scene, id)) {
			it = clips.erase(it);
			continue;
		}

		auto &playback = it->second;
		if (playback.paused) {
			++it;
			continue;
		}

		const auto &clip = *object->animationClip;
		const double duration = clip.duration;
		// ループするTransform Animationと同じように、大きな更新での姿勢の飛びを抑える。
		const double elapsed = playback.seconds +
		    (clip.loop ? (std::min)(seconds, .1f) : seconds);
		const double time = clip.loop ? std::fmod(elapsed, duration)
		                              : (std::min)(elapsed, duration);
		if (!ApplyAnimationClip(scene, clip, time, error))
			return false;

		if (clip.loop && elapsed >= duration) {
			QueueClipMarkers(*object, id, playback.seconds, duration, signals);
			const bool hasMarkers = object->triggerActions &&
			    std::ranges::any_of(object->triggerActions->actions, [](const auto &action) {
				    return action.event == TriggerAction::Event::ANIMATION_CLIP_MARKER;
			    });
			if (hasMarkers) {
				// 極端に短いクリップのマーカーで、1更新の処理量が膨らまないようにする。
				const double completed = std::floor(elapsed / duration);
				if (completed > kMaximumDelayedTriggerActions) {
					error = "Too many Animation Clip loops crossed in one update.";
					return false;
				}

				for (std::size_t loop = 1; loop < static_cast<std::size_t>(completed); ++loop)
					QueueClipMarkers(*object, id, -1, duration, signals);
				QueueClipMarkers(*object, id, -1, time, signals);
			}
		} else {
			QueueClipMarkers(*object, id, playback.seconds, time, signals);
		}

		if (!clip.loop && time >= duration) {
			signals.push_back({id, TriggerAction::Event::ANIMATION_CLIP_FINISHED});
			it = clips.erase(it);
		} else {
			playback.seconds = time;
			++it;
		}
	}

	return true;
}
