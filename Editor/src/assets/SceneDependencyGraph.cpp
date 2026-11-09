#include "assets/SceneDependencyGraph.h"
#include "platform/windows/PathUtils.h"
#include "scene/PrefabSerializer.h"
#include "scene/Scene.h"
#include <algorithm>
#include <stdexcept>

std::vector<SceneDependencyGraph::Entry> SceneDependencyGraph::Read(
    const Scene &scene, const std::filesystem::path &source,
    const Resolve &resolve, const Load &load, const LoadPrefab &loadPrefab) {
	if (!resolve || !load)
		throw std::invalid_argument(
		    "Scene dependency readers must be provided.");
	std::vector<Entry> entries{
	    {source, scene.GetObjects(), false, 0, scene.GetLighting(),
	     scene.UsesLegacyDirectionalLight()}};

	for (std::size_t i = 0; i < entries.size(); ++i) {
		auto objects = entries[i].objects;
		std::vector<std::filesystem::path> targets;

		for (auto &[id, object] : objects) {
			(void)id;
			if (object.script)
				for (auto *bindings :
				     {&object.script->prefabs, &object.script->scenes})
					for (auto &[name, path] : *bindings) {
						(void)name;

						if (!path.empty()) {
							path = resolve(path);
							targets.push_back(path);
						}
					}
			if (!object.triggerActions)
				continue;
			for (auto &action : object.triggerActions->actions) {
				if (action.operation != TriggerAction::Operation::LOAD_SCENE ||
				    action.scenePath.empty())
					continue;
				action.scenePath = resolve(action.scenePath);
				targets.push_back(action.scenePath);
			}
		}

		entries[i].objects = std::move(objects);

		for (const auto &path : targets) {
			if (std::ranges::any_of(entries, [&](const auto &entry) {
				    return !entry.source.empty() &&
				           PathUtils::IsSameFile(entry.source, path);
			    }))
				continue;
			if (entries.size() >= kMaximumScenes)
				throw std::runtime_error(
				    "The scene/Prefab dependency graph exceeds 256 assets.");
			Scene target;
			const bool prefab = PrefabSerializer::IsPrefabPath(path);
			SceneObjectId counter = 0;

			if (prefab) {
				if (!loadPrefab)
					throw std::runtime_error(
					    "A Prefab dependency reader is required.");
				loadPrefab(path, target, counter);
			} else
				load(path, target);
			entries.push_back({path, target.GetObjects(), prefab, counter,
			                   target.GetLighting(), target.UsesLegacyDirectionalLight()});
		}
	}

	return entries;
}
