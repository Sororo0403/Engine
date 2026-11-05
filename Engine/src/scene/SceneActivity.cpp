#include "scene/SceneActivity.h"

#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
#include <vector>

bool SceneActivity::BuildStates(const Scene &scene, States &states) {
	std::vector<SceneObjectId> order;
	if (!SceneHierarchy::GetEvaluationOrder(scene, order)) {
		return false;
	}
	States result;
	for (const auto id : order) {
		const auto &object = *scene.FindObject(id);
		result.emplace(id, object.active && (object.parentId == 0 ||
		                                     result.at(object.parentId)));
	}
	states.swap(result);
	return true;
}

bool SceneActivity::IsActive(const Scene &scene, SceneObjectId id) {
	if (id == 0) {
		return false;
	}
	bool active = true;
	std::size_t visited = 0;
	while (id != 0) {
		const auto *object = scene.FindObject(id);
		if (!object || ++visited > scene.GetObjects().size()) {
			return false;
		}
		active &= object->active;
		id = object->parentId;
	}
	return active;
}
