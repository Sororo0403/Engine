#include "scene/SceneObjects.h"

#include "history/EditorHistory.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "selection/EditorClipboard.h"
#include <map>
#include <string>
#include <utility>

bool SceneObjects::Capture(const Scene &source,
                           std::span<const SceneObjectId> ids,
                           Scene &snapshot) {
	EditorClipboard copied;

	if (!copied.Copy(source, ids)) {
		return false;
	}

	copied.DetachExternalReferences();

	std::map<SceneObjectId, SceneObject> objects;
	const auto values = copied.GetObjects();
	const auto sourceIds = copied.GetSourceIds();

	for (std::size_t i = 0; i < sourceIds.size(); ++i) {
		objects.emplace(sourceIds[i], values[i]);
	}

	Scene candidate;
	candidate.ReplaceObjects(objects);
	std::string text;

	if (!SceneSerializer::Serialize(candidate, text)) {
		return false;
	}

	// 検証後に置き換え、失敗時は以前のスナップショットを保持する。
	snapshot.ReplaceObjects(std::move(objects));
	return true;
}

std::vector<SceneObjectId> SceneObjects::Append(const Scene &source,
                                                EditorHistory &history) {
	std::vector<SceneObjectId> ids;
	ids.reserve(source.GetObjects().size());

	for (const auto &[id, object] : source.GetObjects()) {
		ids.push_back(id);
	}

	Scene snapshot;

	if (!Capture(source, ids, snapshot)) {
		return {};
	}

	std::vector<SceneObject> objects;
	objects.reserve(ids.size());

	for (const auto id : ids) {
		objects.push_back(*snapshot.FindObject(id));
	}

	return history.CreateObjects(std::move(objects), ids);
}
