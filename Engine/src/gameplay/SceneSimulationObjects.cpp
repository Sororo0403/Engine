#include "gameplay/SceneSimulation.h"
#include "scene/PrefabInstantiation.h"
#include "scene/SceneDestruction.h"
#include <exception>

SceneObjectId
SceneSimulation::InstantiatePrefab(const Scene &prefab,
                                   const DirectX::XMFLOAT3 &position,
                                   std::string &error) {
	if (!playing_) {
		error = "Start Play before instantiating a Prefab.";
		return 0;
	}

	try {
		Scene candidate;
		candidate.CopyFrom(scene_);
		const auto created = PrefabInstantiation::Instantiate(prefab, candidate,
		                                                      position, error);
		if (!created)
			return 0;
		SceneSimulation next;
		next.CopyFrom(*this);
		if (!next.Configure(candidate, error, fallbackPose_, nullptr, true))
			return 0;
		const auto events = next.triggers_.GetEvents();
		next.frameEvents_.assign(events.begin(), events.end());
		next.QueueActionSignals(events, next.pendingActionSignals_);
		next.scripts_.QueueTriggers(events);
		Adopt(next);
		error.clear();
		return created;
	} catch (const std::exception &exception) {
		error = exception.what();
		return 0;
	}
}

bool SceneSimulation::DestroyObjects(std::span<const SceneObjectId> roots,
                                     std::string &error) {
	if (!playing_ || roots.empty()) {
		error = "Start Play and specify roots before destroying objects.";
		return false;
	}

	try {
		Scene candidate;
		candidate.CopyFrom(scene_);
		if (!SceneDestruction::Apply(candidate, roots, error))
			return false;
		SceneSimulation next;
		next.CopyFrom(*this);
		std::vector<TriggerEvent> generated;
		if (!next.SynchronizeSceneChanges(candidate, {}, error, nullptr,
		                                  &generated))
			return false;
		next.scripts_.QueueTriggers(generated);
		Adopt(next);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
