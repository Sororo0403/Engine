#include "history/TextureSlotsCommand.h"
#include "scene/Scene.h"
#include <utility>

TextureSlotsCommand::TextureSlotsCommand(SceneObjectId id,
                                         TextureBindings before,
                                         TextureBindings after)
    : id_(id), before_(std::move(before)), after_(std::move(after)) {
}

bool TextureSlotsCommand::Undo(Scene &scene) const {
	return Apply(scene, after_, before_);
}

bool TextureSlotsCommand::Redo(Scene &scene) const {
	return Apply(scene, before_, after_);
}

void TextureSlotsCommand::RewriteObjects(
    const SceneAssetReferences::ObjectRewrite &rewrite) {
	SceneObject object;
	auto &textures = object.meshRenderer.emplace().textures;
	for (auto *value : {&before_, &after_}) {
		textures.slots.swap(value->slots);
		rewrite(object);
		textures.slots.swap(value->slots);
	}
}

bool TextureSlotsCommand::Apply(Scene &scene, const TextureBindings &expected,
                                const TextureBindings &value) const {
	auto *object = scene.FindObject(id_);

	if (!object || !object->meshRenderer ||
	    object->meshRenderer->textures != expected || !value.IsValid()) {
		return false;
	}

	auto next = value;
	object->meshRenderer->textures.slots.swap(next.slots);
	return true;
}
