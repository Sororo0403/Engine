#include "history/TransformCommand.h"

#include "math/TransformUtils.h"
#include "scene/Scene.h"

TransformCommand::TransformCommand(SceneObjectId objectId,
                                   const Transform &before,
                                   const Transform &after) noexcept
    : objectId_(objectId), before_(before), after_(after) {
}

bool TransformCommand::HasChanges() const {
	return !TransformUtils::AreEqual(before_, after_);
}

bool TransformCommand::Undo(Scene &scene) const {
	SceneObject *object = scene.FindObject(objectId_);

	if (!object || !TransformUtils::AreEqual(object->transform, after_)) {
		return false;
	}

	object->transform = before_;
	return true;
}

bool TransformCommand::Redo(Scene &scene) const {
	SceneObject *object = scene.FindObject(objectId_);

	if (!object || !TransformUtils::AreEqual(object->transform, before_)) {
		return false;
	}

	object->transform = after_;
	return true;
}
