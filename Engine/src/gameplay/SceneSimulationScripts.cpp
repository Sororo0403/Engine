#include "gameplay/SceneCameraUtils.h"
#include "gameplay/SceneSimulation.h"
#include "math/TransformUtils.h"
#include "scene/SceneSerializer.h"
#include <utility>

bool SceneSimulation::ApplyScripts(float seconds, const CharacterInput &input,
                                   bool startOnly, std::string &error,
                                   SceneObjectId *invalidObject,
                                   std::span<const GameActionState> actions,
                                   const UIInput *viewport) {
	if (!scripts_.HasInstances())
		return true;
	Scene candidate;
	candidate.CopyFrom(scene_);
	auto scripts = scripts_;
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
	        cameraTransition_ ? &transitionView : nullptr, &clipPauses))
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

	if (candidate.GetObjects() == scene_.GetObjects()) {
		scene_.CopyFrom(candidate);
		audioActions_ = std::move(audio);
		if (!requestedScene.empty())
			pendingScene_ = {std::move(requestedScene), {}};
		sharedScriptData_ = std::move(game);
		scripts_ = std::move(scripts);
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
	return true;
}
