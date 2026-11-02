#include "history/MaterialSlotsCommand.h"
#include "scene/Scene.h"
#include <utility>

MaterialSlotsCommand::MaterialSlotsCommand(SceneObjectId id,
                                           MaterialBindings before,
                                           MaterialBindings after)
    : id_(id), before_(std::move(before)), after_(std::move(after)) {
}

bool MaterialSlotsCommand::Undo(Scene &scene) const {
	return Apply(scene, after_, before_);
}

bool MaterialSlotsCommand::Redo(Scene &scene) const {
	return Apply(scene, before_, after_);
}

bool MaterialSlotsCommand::Apply(Scene &scene, const MaterialBindings &expected,
                                 const MaterialBindings &value) const {
	auto *object = scene.FindObject(id_);

	if (!object || !object->meshRenderer ||
	    object->meshRenderer->materials != expected || !value.IsValid()) {
		return false;
	}

	auto next = value;
	object->meshRenderer->materials.slots.swap(next.slots);
	return true;
}
