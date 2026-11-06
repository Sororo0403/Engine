#include "assets/SceneDependencyGraph.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include <algorithm>
#include <stdexcept>

std::vector<SceneDependencyGraph::Entry>
SceneDependencyGraph::Read(const Scene &scene,
                           const std::filesystem::path &source,
                           const Resolve &resolve, const Load &load) {
	if (!resolve || !load)
		throw std::invalid_argument(
		    "Scene dependency readers must be provided.");
	std::vector<Entry> entries{{source, scene.GetObjects()}};
	for (std::size_t i = 0; i < entries.size(); ++i) {
		auto objects = entries[i].objects;
		std::vector<std::filesystem::path> targets;
		for (auto &[id, object] : objects) {
			(void)id;
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
				    "The scene transition graph exceeds 256 scenes.");
			Scene target;
			load(path, target);
			entries.push_back({path, target.GetObjects()});
		}
	}
	return entries;
}
