#include "history/ModelPathCommand.h"

#include "scene/Scene.h"

#include <utility>

ModelPathCommand::ModelPathCommand(SceneObjectId objectId,
                                   std::filesystem::path before,
                                   std::filesystem::path after) noexcept
    : objectId_(objectId), before_(std::move(before)),
      after_(std::move(after)) {
}

bool ModelPathCommand::HasChanges() const {
	return before_ != after_;
}

bool ModelPathCommand::Undo(Scene &scene) const {
	auto *object = scene.FindObject(objectId_);

	if (!object || object->modelPath != after_) {
		return false;
	}

	// コピーに成功してから参照先だけを置き換える
	auto restored = before_;
	object->modelPath.swap(restored);
	return true;
}

bool ModelPathCommand::Redo(Scene &scene) const {
	auto *object = scene.FindObject(objectId_);

	if (!object || object->modelPath != before_) {
		return false;
	}

	auto restored = after_;
	object->modelPath.swap(restored);
	return true;
}
