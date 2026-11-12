#include "scene/SceneAssetReferences.h"
#include "scene/Scene.h"
#include <stdexcept>

namespace {
template <class Object, class Apply>
void ForEach(Object &object, const Apply &apply) {
	if (object.triggerActions)
		for (auto &action : object.triggerActions->actions)
			if (action.operation == TriggerAction::Operation::LOAD_SCENE)
				apply(action.scenePath);
	if (object.script) {
		apply(object.script->asset);
		for (auto *bindings :
		     {&object.script->prefabs, &object.script->scenes})
			for (auto &[name, path] : *bindings) {
				(void)name;
				apply(path);
			}
	}

	if (object.animationClip && object.animationClip->usesAsset)
		apply(object.animationClip->asset);

	if (object.particleEmitter)
		apply(object.particleEmitter->texture);
	if (object.image)
		apply(object.image->texture);
	if (object.mesh)
		apply(object.mesh->modelPath);
	if (object.meshRenderer)
		for (auto &[slot, path] : object.meshRenderer->textures.slots) {
			(void)slot;
			apply(path);
		}
	if (object.audioSource)
		apply(object.audioSource->clipPath);
}
} // namespace

void SceneAssetReferences::Visit(
    const SceneObject &object,
    const std::function<void(const std::filesystem::path &)> &visit) {
	ForEach(object, [&](const auto &path) {
		if (!path.empty()) {
			visit(path);
		}
	});
}

void SceneAssetReferences::Rewrite(
    SceneObject &object,
    const std::function<std::filesystem::path(const std::filesystem::path &)>
        &rewrite) {
	if (!rewrite)
		throw std::invalid_argument("An asset reference resolver is required.");
	ForEach(object, [&](auto &path) {
		if (!path.empty())
			path = rewrite(path);
	});
}

void SceneAssetReferences::Rewrite(
    Scene &scene,
    const std::function<std::filesystem::path(const std::filesystem::path &)>
        &rewrite) {
	if (!rewrite)
		throw std::invalid_argument("An asset reference resolver is required.");
	for (const auto &[id, value] : scene.GetObjects()) {
		(void)value;
		Rewrite(*scene.FindObject(id), rewrite);
	}
}
