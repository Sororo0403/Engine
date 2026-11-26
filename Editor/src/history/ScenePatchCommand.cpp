#include "history/ScenePatchCommand.h"

#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include <set>

ScenePatchCommand::ScenePatchCommand(const Scene &before, const Scene &after) {
	std::set<SceneObjectId> ids;

	for (const auto &[id, object] : before.GetObjects()) {
		(void)object;
		ids.insert(id);
	}

	for (const auto &[id, object] : after.GetObjects()) {
		(void)object;
		ids.insert(id);
	}

	for (const auto id : ids) {
		const auto *old = before.FindObject(id), *next = after.FindObject(id);

		if (old && next && *old == *next) {
			continue;
		}

		before_.emplace_back(id, old ? std::optional{*old} : std::nullopt);
		after_.emplace_back(id, next ? std::optional{*next} : std::nullopt);
	}
}

bool ScenePatchCommand::HasChanges() const {
	return !before_.empty();
}

bool ScenePatchCommand::Undo(Scene &scene) const {
	return Apply(scene, after_, before_);
}

bool ScenePatchCommand::Redo(Scene &scene) const {
	return Apply(scene, before_, after_);
}

bool ScenePatchCommand::Apply(Scene &scene, const Values &expected,
                              const Values &values) const {
	for (const auto &[id, value] : expected) {
		const auto *current = scene.FindObject(id);

		if ((bool(current) != bool(value)) ||
		    (current && !(*current == *value))) {
			return false;
		}
	}

	return scene.ApplyTransaction([&](Scene &candidate) {
		auto objects = candidate.GetObjects();

		for (const auto &[id, value] : values) {
			if (value) {
				objects.erase(id);
				objects.try_emplace(id, *value);
			} else {
				objects.erase(id);
			}
		}

		candidate.ReplaceObjects(std::move(objects));
		std::string text;
		return SceneSerializer::Serialize(candidate, text);
	});
}
