#include "scene/SceneDestruction.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneObjectReferences.h"
#include "scene/SceneSerializer.h"
#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

bool SceneDestruction::Apply(Scene &scene, std::span<const SceneObjectId> roots,
                             std::string &error) {
	try {
		error.clear();
		if (roots.empty())
			return true;
		std::set<SceneObjectId> requested;
		for (auto id : roots) {
			if (!scene.Contains(id))
				throw std::runtime_error(
				    "Destruction requires existing roots.");
			requested.insert(id);
		}
		std::vector<SceneObjectId> order;
		if (!SceneHierarchy::GetEvaluationOrder(scene, order))
			throw std::runtime_error("Destruction hierarchy is invalid.");
		std::map<SceneObjectId, SceneObjectId> removed;
		for (auto id : order)
			if (requested.contains(id) ||
			    removed.contains(scene.FindObject(id)->parentId))
				removed.emplace(id, 0);
		if (!scene.ApplyTransaction([&](Scene &working) {
			    auto objects = working.GetObjects();
			    for (auto id : order) {
				    if (removed.contains(id)) {
					    objects.erase(id);
					    continue;
				    }
				    auto &o = objects.at(id);
				    if (o.followCamera &&
				        removed.contains(o.followCamera->target))
					    o.followCamera.reset();
				    if (o.triggerActions)
					    std::erase_if(
					        o.triggerActions->actions, [&](const auto &action) {
						        const bool targetRemoved =
						            removed.contains(action.target);
						        const bool destinationRemoved =
						            action.operation ==
						                TriggerAction::Operation::TELEPORT &&
						            removed.contains(action.destination);
						        return action.operation !=
						                   TriggerAction::Operation::
						                       LOAD_SCENE &&
						               (targetRemoved || destinationRemoved);
					        });
				    if (o.prefabLink && removed.contains(o.prefabLink->root))
					    o.prefabLink.reset();
				    SceneObjectReferences::Remap(o, removed);
			    }
			    working.ReplaceObjects(std::move(objects));
			    std::string serialized;
			    if (!SceneSerializer::Serialize(working, serialized))
				    throw std::runtime_error(
				        "Destruction produced invalid scene data.");
			    return true;
		    }))
			throw std::runtime_error("Could not apply object destruction.");
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
