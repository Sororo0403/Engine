#include "gameplay/SceneSimulation.h"
#include "scene/PrefabInstantiation.h"
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
