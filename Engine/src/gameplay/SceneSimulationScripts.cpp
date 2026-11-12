#include "gameplay/SceneCameraUtils.h"
#include "gameplay/SceneSimulation.h"
#include "graphics/camera/Camera.h"
#include "math/TransformUtils.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneSerializer.h"
#include <algorithm>
#include <utility>

bool SceneSimulation::ApplyScripts(float seconds, const CharacterInput &input,
                                   bool startOnly, std::string &error,
                                   SceneObjectId *invalidObject,
                                   std::span<const GameActionState> actions,
                                   const UIInput *viewport) {
	if (!scripts_.HasInstances()) {
		characterAirflow_ = {};
		return true;
	}
	Scene candidate;
	candidate.CopyFrom(scene_);
	auto scripts = scripts_;
	std::array<float, 3> airflow{};
	auto physics = physics_;
	auto animations = animations_;
	auto particles = particles_;
	auto animators = animators_;
	auto audio = audioActions_;
	std::filesystem::path requestedScene;
	auto game = sharedScriptData_;
	std::map<SceneObjectId, bool> clipPauses;
	// すべてのupdateに、物理・Trigger処理後でLua変更前の同じ主人公状態を渡す。
	const auto character = startOnly ? std::nullopt : GetCharacterState();
	const CameraTransitionView transitionView{camera_, pose_};
	if (!scripts.Run(
	        candidate, seconds, input, frameEvents_, startOnly, error,
	        invalidObject,
	        startOnly ? std::span<const SceneObjectId>{} : ui_.GetClicks(),
	        &physics,
	        startOnly ? std::span<const CollisionEvent>{}
	                  : collisions_.GetEvents(),
	        &audio, &requestedScene, &game, actions, &animations, &particles,
	        &animators, viewport, character ? &*character : nullptr,
	        cameraTransition_ ? &transitionView : nullptr, &clipPauses,
	        &airflow))
		return false;
	std::string serialized;

	const bool valid = startOnly
	                       ? SceneSerializer::Serialize(candidate, serialized)
	                       : SceneSerializer::ValidateChanges(candidate, scene_);
	if (!valid) {
		error = "Script changes contain invalid scene data.";
		return false;
	}
	const auto applyClipPauses = [&](SceneSimulation &simulation) {
		for (const auto &[id, paused] : clipPauses) {
			const auto playing = simulation.animationClips_.find(id);
			if (playing != simulation.animationClips_.end())
				playing->second.paused = paused;
		}
	};

	const auto &beforeObjects = scene_.GetObjects();
	const auto &afterObjects = candidate.GetObjects();
	std::optional<CameraPose> updatedPose;
	// 描画と音量だけが変わるフレームは、物理やTriggerの状態を保つ。
	const bool presentationOnly = std::ranges::equal(
	    beforeObjects, afterObjects, [&](const auto &before, const auto &after) {
		    if (before.first != after.first)
			    return false;
		    const auto &oldObject = before.second;
		    const auto &newObject = after.second;
		    if (oldObject.HasSameSimulationSettings(newObject))
			    return true;
		    if (startOnly)
			    return false;

		    auto compared = newObject;
		    if (compared.audioSource && oldObject.audioSource)
			    compared.audioSource->volume = oldObject.audioSource->volume;

		    const bool moved = !TransformUtils::AreEqual(oldObject.transform,
		                                                newObject.transform);
		    if (moved) {
			    // 他の物体や物理が参照する姿勢は、通常の同期処理で更新する。
			    if (after.first != camera_ || cameraTransition_ ||
			        !newObject.camera || newObject.parentId || newObject.collider ||
			        newObject.rigidBody || newObject.character || newObject.followCamera ||
			        newObject.motionPath || newObject.transformAnimation ||
			        std::ranges::any_of(afterObjects, [&](const auto &entry) {
				        const auto &object = entry.second;
				        return object.parentId == camera_ ||
				               (object.followCamera && object.followCamera->target == camera_);
			        }))
				    return false;
			    compared.transform = oldObject.transform;
		    }
		    if (!oldObject.HasSameSimulationSettings(compared))
			    return false;
		    if (moved) {
			    SceneWorldTransform transform;
			    if (!SceneHierarchy::GetWorldTransform(candidate, camera_, transform))
				    return false;
			    const auto pose = SceneCameraUtils::FromWorldTransform(transform);
			    Camera validator;
			    if (!validator.SetPose(pose))
				    return false;
			    updatedPose = pose;
		    }
		    return true;
	    });
	if (presentationOnly) {
		if (updatedPose)
			pose_ = *updatedPose;
		scene_.AdoptValidated(candidate);
		audioActions_ = std::move(audio);
		if (!requestedScene.empty())
			pendingScene_ = {std::move(requestedScene), {}};
		sharedScriptData_ = std::move(game);
		scripts_ = std::move(scripts);
		characterAirflow_ = airflow;
		physics_ = std::move(physics);
		animations_ = std::move(animations);
		particles_ = std::move(particles);
		animators_ = std::move(animators);
		applyClipPauses(*this);
		return true;
	}

	std::vector<SceneObjectId> transformed;

	for (const auto &[id, object] : candidate.GetObjects()) {
		const auto *before = scene_.FindObject(id);
		if (!before)
			continue;
		if (!TransformUtils::AreEqual(before->transform, object.transform))
			transformed.push_back(id);
	}

	SceneSimulation next;
	next.CopyFrom(*this);
	next.scripts_ = std::move(scripts);
	next.physics_ = std::move(physics);
	next.animations_ = std::move(animations);
	next.particles_ = std::move(particles);
	next.animators_ = std::move(animators);
	next.audioActions_ = std::move(audio);
	if (!requestedScene.empty())
		next.pendingScene_ = {std::move(requestedScene), {}};
	next.sharedScriptData_ = std::move(game);
	std::vector<TriggerEvent> generated;
	if (!next.SynchronizeSceneChanges(candidate, transformed, error,
	                                  invalidObject, &generated))
		return false;
	if (!startOnly)
		next.scripts_.QueueTriggers(generated);
	applyClipPauses(next);
	Adopt(next);
	characterAirflow_ = airflow;
	return true;
}
