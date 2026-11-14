#include "scene/SceneAssetReferences.h"
#include "scene/Scene.h"
#include <stdexcept>

namespace {
using SceneAssetReferences::Kind;

template <class Object, class Apply>
void ForEach(Object &object, const Apply &apply) {
	if (object.triggerActions)
		for (auto &action : object.triggerActions->actions)
			if (action.operation == TriggerAction::Operation::LOAD_SCENE)
				apply(action.scenePath, Kind::SCENE);
	if (object.script) {
		apply(object.script->asset, Kind::ASSET);
		for (auto &[name, path] : object.script->prefabs) {
			(void)name;
			apply(path, Kind::PREFAB);
		}
		for (auto &[name, path] : object.script->scenes) {
			(void)name;
			apply(path, Kind::SCENE);
		}
	}

	if (object.animationClip && object.animationClip->usesAsset)
		apply(object.animationClip->asset, Kind::ASSET);

	if (object.particleEmitter)
		apply(object.particleEmitter->texture, Kind::ASSET);
	if (object.image)
		apply(object.image->texture, Kind::ASSET);
	if (object.mesh)
		apply(object.mesh->modelPath, Kind::MODEL);
	if (object.meshRenderer)
		for (auto &[slot, path] : object.meshRenderer->textures.slots) {
			(void)slot;
			apply(path, Kind::ASSET);
		}
	if (object.audioSource)
		apply(object.audioSource->clipPath, Kind::ASSET);
}
} // namespace

void SceneAssetReferences::Visit(
    const SceneObject &object,
    const std::function<void(const std::filesystem::path &)> &visit) {
	Visit(object, [&](const auto &path, Kind) { visit(path); });
}

void SceneAssetReferences::Visit(
    const SceneObject &object,
    const std::function<void(const std::filesystem::path &, Kind)> &visit) {
	ForEach(object, [&](const auto &path, Kind kind) {
		if (!path.empty()) {
			visit(path, kind);
		}
	});
}

void SceneAssetReferences::Rewrite(
    SceneObject &object,
    const std::function<std::filesystem::path(const std::filesystem::path &)>
        &rewrite) {
	if (!rewrite)
		throw std::invalid_argument("An asset reference resolver is required.");
	ForEach(object, [&](auto &path, Kind) {
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
