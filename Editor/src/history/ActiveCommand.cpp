#include "history/ActiveCommand.h"
#include "scene/Scene.h"

ActiveCommand::ActiveCommand(SceneObjectId objectId, bool before,
                             bool after) noexcept
    : objectId_(objectId), before_(before), after_(after) {
}

bool ActiveCommand::Undo(Scene &scene) const {
	auto *object = scene.FindObject(objectId_);

	if (!object || object->active != after_) {
		return false;
	}

	object->active = before_;
	return true;
}

bool ActiveCommand::Redo(Scene &scene) const {
	auto *object = scene.FindObject(objectId_);

	if (!object || object->active != before_) {
		return false;
	}

	object->active = after_;
	return true;
}
