#include "gameplay/FollowCameraUsage.h"

#include "scene/Scene.h"
#include "selection/EditorSelection.h"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

std::vector<SceneObjectId>
FollowCameraUsage::FindObjects(const Scene &scene,
                               std::span<const SceneObjectId> targets) {
	std::set<SceneObjectId> unique;

	for (const auto id : targets) {
		if (id != 0 && scene.Contains(id)) {
			unique.insert(id);
		}
	}

	std::vector<SceneObjectId> result;

	if (unique.empty()) {
		return result;
	}

	for (const auto &[id, object] : scene.GetObjects()) {
		if (object.followCamera &&
		    unique.contains(object.followCamera->target)) {
			result.push_back(id);
		}
	}

	return result;
}

std::size_t FollowCameraUsage::SelectObjects(const Scene &scene,
                                             EditorSelection &selection) {
	auto ids = FindObjects(scene, selection.GetIds());
	const auto active =
	    std::find(ids.begin(), ids.end(), selection.GetActiveId());

	if (active != ids.end()) {
		std::rotate(active, active + 1, ids.end());
	}

	const auto count = ids.size();

	if (!selection.Set(std::move(ids))) {
		throw std::logic_error(
		    "FollowCameraUsage: 選択する物体の一覧が無効です。");
	}

	return count;
}
