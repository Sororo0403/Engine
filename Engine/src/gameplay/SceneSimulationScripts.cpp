#include "gameplay/SceneSimulation.h"
#include "math/TransformUtils.h"
#include "scene/SceneSerializer.h"
#include <utility>

bool SceneSimulation::ApplyScripts(float seconds, const CharacterInput &input,
                                   bool startOnly, std::string &error,
                                   SceneObjectId *invalidObject) {
	if (!scripts_.HasInstances())
		return true;
	Scene candidate;
	candidate.CopyFrom(scene_);
	auto scripts = scripts_;
	auto physics = physics_;
	auto audio = audioActions_;
	auto requestedScene = pendingScene_;
	auto game = sharedScriptData_;
	if (!scripts.Run(candidate, seconds, input, frameEvents_, startOnly, error,
	                 invalidObject,
	                 startOnly ? std::span<const SceneObjectId>{}
	                           : ui_.GetClicks(),
	                 &physics,
	                 startOnly ? std::span<const CollisionEvent>{}
	                           : collisions_.GetEvents(),
	                 &audio, &requestedScene, &game))
		return false;
	std::string serialized;
	if (!SceneSerializer::Serialize(candidate, serialized)) {
		error = "Script changes contain invalid scene data.";
		return false;
	}
	if (candidate.GetObjects() == scene_.GetObjects()) {
		scene_.CopyFrom(candidate);
		audioActions_ = std::move(audio);
		pendingScene_ = std::move(requestedScene);
		sharedScriptData_ = std::move(game);
		scripts_ = std::move(scripts);
		physics_ = std::move(physics);
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
	next.audioActions_ = std::move(audio);
	next.pendingScene_ = std::move(requestedScene);
	next.sharedScriptData_ = std::move(game);
	std::vector<TriggerEvent> generated;
	if (!next.SynchronizeSceneChanges(candidate, transformed, error,
	                                  invalidObject, &generated))
		return false;
	if (!startOnly)
		next.scripts_.QueueTriggers(generated);
	Adopt(next);
	return true;
}
