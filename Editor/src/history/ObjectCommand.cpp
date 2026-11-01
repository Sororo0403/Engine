#include "history/ObjectCommand.h"

#include "math/TransformUtils.h"
#include "scene/Scene.h"

#include <utility>

ObjectCommand::ObjectCommand(SceneObjectId objectId, SceneObject object,
                             Kind kind) noexcept
    : objectId_(objectId), object_(std::move(object)), kind_(kind) {}

bool ObjectCommand::Undo(Scene &scene) const {
	if (kind_ == Kind::CREATE) {
		return Remove(scene);
	}

	return scene.RestoreObject(objectId_, object_);
}

bool ObjectCommand::Redo(Scene &scene) const {
	if (kind_ == Kind::CREATE) {
		return scene.RestoreObject(objectId_, object_);
	}

	return Remove(scene);
}

bool ObjectCommand::Remove(Scene &scene) const {
	const SceneObject *current = scene.FindObject(objectId_);

	if (!current || current->name != object_.name ||
	    current->modelPath != object_.modelPath || current->visible != object_.visible ||
	    !TransformUtils::AreEqual(current->transform, object_.transform)) {
		return false;
	}

	return scene.DestroyObject(objectId_);
}
