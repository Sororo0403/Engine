#include "scene/SceneAssetReferences.h"
#include "scene/Scene.h"
#include <stdexcept>

void SceneAssetReferences::Rewrite(
    Scene &scene,
    const std::function<std::filesystem::path(const std::filesystem::path &)>
        &rewrite) {
	if (!rewrite)
		throw std::invalid_argument("An asset reference resolver is required.");
	const auto apply = [&](auto &path) {
		if (!path.empty())
			path = rewrite(path);
	};
	for (const auto &[id, value] : scene.GetObjects()) {
		(void)value;
		auto &object = *scene.FindObject(id);
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
}
