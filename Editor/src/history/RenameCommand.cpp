#include "history/RenameCommand.h"

#include "scene/Scene.h"

#include <utility>

RenameCommand::RenameCommand(SceneObjectId objectId, std::string before,
                             std::string after) noexcept
    : objectId_(objectId), before_(std::move(before)),
      after_(std::move(after)) {
}

bool RenameCommand::HasChanges() const {
	return before_ != after_;
}

bool RenameCommand::Undo(Scene &scene) const {
	SceneObject *object = scene.FindObject(objectId_);

	if (!object || object->name != after_) {
		return false;
	}

	// コピーに成功してから、現在の名前を置き換える
	std::string restored = before_;
	object->name.swap(restored);

	return true;
}

bool RenameCommand::Redo(Scene &scene) const {
	SceneObject *object = scene.FindObject(objectId_);

	if (!object || object->name != before_) {
		return false;
	}

	std::string restored = after_;
	object->name.swap(restored);

	return true;
}
