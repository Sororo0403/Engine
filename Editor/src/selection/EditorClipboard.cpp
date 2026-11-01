#include "selection/EditorClipboard.h"

#include "math/TransformUtils.h"
#include "scene/Scene.h"

#include <set>

bool EditorClipboard::Copy(
    const Scene &scene, std::span<const SceneObjectId> objectIds) {
	if (objectIds.empty()) {
		return false;
	}
	std::set<SceneObjectId> seen;
	std::vector<SceneObject> objects;
	objects.reserve(objectIds.size());
	for (const SceneObjectId id : objectIds) {
		if (id == 0 || !seen.insert(id).second) {
			return false;
		}
		const SceneObject *object = scene.FindObject(id);
		if (!object || !TransformUtils::IsValid(object->transform)) {
			return false;
		}
		objects.push_back(*object);
	}
	objects_.swap(objects);
	return true;
}

std::span<const SceneObject> EditorClipboard::GetObjects() const {
	return objects_;
}

bool EditorClipboard::HasObjects() const { return !objects_.empty(); }

void EditorClipboard::Clear() { objects_.clear(); }
