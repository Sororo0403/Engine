#include "history/VisibilityCommand.h"

#include "scene/Scene.h"

VisibilityCommand::VisibilityCommand(SceneObjectId objectId, bool before,
                                     bool after) noexcept
    : objectId_(objectId), before_(before), after_(after) {
}

bool VisibilityCommand::Undo(Scene &scene) const {
	auto *object = scene.FindObject(objectId_);

	if (!object || !object->meshRenderer ||
	    object->meshRenderer->visible != after_) {
		return false;
	}

	object->meshRenderer->visible = before_;
	return true;
}

bool VisibilityCommand::Redo(Scene &scene) const {
	auto *object = scene.FindObject(objectId_);

	if (!object || !object->meshRenderer ||
	    object->meshRenderer->visible != before_) {
		return false;
	}

	object->meshRenderer->visible = after_;
	return true;
}
