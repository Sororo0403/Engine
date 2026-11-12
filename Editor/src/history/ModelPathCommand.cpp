#include "history/ModelPathCommand.h"

#include "scene/Scene.h"

#include <utility>

ModelPathCommand::ModelPathCommand(SceneObjectId objectId,
                                   std::optional<MeshComponent> before,
                                   std::optional<MeshComponent> after) noexcept
    : objectId_(objectId), before_(std::move(before)),
      after_(std::move(after)) {
}

bool ModelPathCommand::HasChanges() const {
	return before_ != after_;
}

bool ModelPathCommand::Undo(Scene &scene) const {
	auto *object = scene.FindObject(objectId_);

	if (!object || object->mesh != after_ || (before_ && !before_->IsValid())) {
		return false;
	}

	// コピーに成功してから参照先だけを置き換える
	auto restored = before_;
	object->mesh.swap(restored);
	return true;
}

bool ModelPathCommand::Redo(Scene &scene) const {
	auto *object = scene.FindObject(objectId_);

	if (!object || object->mesh != before_ || (after_ && !after_->IsValid())) {
		return false;
	}

	auto restored = after_;
	object->mesh.swap(restored);
	return true;
}

void ModelPathCommand::RewriteObjects(
    const SceneAssetReferences::ObjectRewrite &rewrite) {
	for (auto *value : {&before_, &after_}) {
		if (*value) {
			SceneObject object;
			object.mesh.swap(*value);
			rewrite(object);
			object.mesh.swap(*value);
		}
	}
}
