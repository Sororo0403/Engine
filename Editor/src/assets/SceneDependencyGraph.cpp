#include "assets/SceneDependencyGraph.h"
#include "platform/windows/PathUtils.h"
#include "scene/PrefabSerializer.h"
#include "scene/Scene.h"
#include <algorithm>
#include <format>
#include <stdexcept>

namespace {
/// <summary>
/// 遷移先やPrefabの読み込みに失敗した場合に参照元も示す
/// </summary>
std::runtime_error DependencyError(const std::filesystem::path &source,
                                   const SceneObject &object,
                                   SceneObjectId id,
                                   const std::filesystem::path &target,
                                   const std::exception &error) {
	return std::runtime_error(std::format(
	    "{} / {} (ID {}) references {}\n{}",
	    source.empty() ? "Startup scene" : PathUtils::ToUtf8(source),
	    object.name, id, PathUtils::ToUtf8(target), error.what()));
}
} // namespace

std::vector<SceneDependencyGraph::Entry> SceneDependencyGraph::Read(
    const Scene &scene, const std::filesystem::path &source,
    const Resolve &resolve, const Load &load, const LoadPrefab &loadPrefab) {
	if (!resolve || !load)
		throw std::invalid_argument(
		    "Scene dependency readers must be provided.");
	std::vector<Entry> entries{
	    {source, scene.GetObjects(), false, 0, scene.GetLighting(),
	     scene.UsesLegacyDirectionalLight(), scene.GetFog()}};

	for (std::size_t i = 0; i < entries.size(); ++i) {
		auto objects = entries[i].objects;
		std::vector<std::pair<std::filesystem::path, SceneObjectId>> targets;

		for (auto &[id, object] : objects) {
			const auto addTarget = [&](std::filesystem::path &path) {
				try {
					path = resolve(path);
				} catch (const std::exception &error) {
					throw DependencyError(entries[i].source, object, id, path,
					                      error);
				}
				targets.emplace_back(path, id);
			};
			if (object.script)
				for (auto *bindings :
				     {&object.script->prefabs, &object.script->scenes})
					for (auto &[name, path] : *bindings) {
						(void)name;

						if (!path.empty()) {
							addTarget(path);
						}
					}
			if (!object.triggerActions)
				continue;
			for (auto &action : object.triggerActions->actions) {
				if (action.operation != TriggerAction::Operation::LOAD_SCENE ||
				    action.scenePath.empty())
					continue;
				addTarget(action.scenePath);
			}
		}

		entries[i].objects = std::move(objects);

		for (const auto &[path, referringId] : targets) {
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

			try {
				if (prefab) {
					if (!loadPrefab)
						throw std::runtime_error(
						    "A Prefab dependency reader is required.");
					loadPrefab(path, target, counter);
				} else {
					load(path, target);
				}
			} catch (const std::exception &error) {
				throw DependencyError(entries[i].source,
				                      entries[i].objects.at(referringId),
				                      referringId, path, error);
			}
			entries.push_back({path, target.GetObjects(), prefab, counter,
			                   target.GetLighting(), target.UsesLegacyDirectionalLight(),
			                   target.GetFog()});
		}
	}

	return entries;
}
