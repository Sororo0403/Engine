#include "scene/Scene.h"

#include <limits>
#include <stdexcept>
#include <utility>

SceneObjectId Scene::CreateObject(std::string name,
                                  std::filesystem::path modelPath) {
	if (nextObjectId_ == (std::numeric_limits<SceneObjectId>::max)()) {
		throw std::overflow_error("Scene: 物体番号の上限に達しました。");
	}

	if (name.empty()) {
		name = "Object";
	}

	SceneObject object;
	object.name = std::move(name);
	object.modelPath = std::move(modelPath);

	const SceneObjectId id = nextObjectId_;

	// 格納に成功してから次の番号へ進める
	objects_.emplace(id, std::move(object));
	++nextObjectId_;

	return id;
}

bool Scene::DestroyObject(SceneObjectId id) { return objects_.erase(id) != 0; }

bool Scene::Contains(SceneObjectId id) const { return objects_.contains(id); }

SceneObject *Scene::FindObject(SceneObjectId id) {
	const auto found = objects_.find(id);

	if (found == objects_.end()) {
		return nullptr;
	}

	return &found->second;
}

const SceneObject *Scene::FindObject(SceneObjectId id) const {
	const auto found = objects_.find(id);

	if (found == objects_.end()) {
		return nullptr;
	}

	return &found->second;
}

const std::map<SceneObjectId, SceneObject> &Scene::GetObjects() const {
	return objects_;
}

void Scene::Clear() { objects_.clear(); }

void Scene::ReplaceObjects(std::map<SceneObjectId, SceneObject> objects) {
	SceneObjectId nextId = nextObjectId_;

	for (const auto &entry : objects) {
		const SceneObjectId id = entry.first;

		if (id == 0 || id == (std::numeric_limits<SceneObjectId>::max)()) {
			throw std::invalid_argument(
			    "Scene::ReplaceObjects: 物体のIDが無効です。");
		}

		if (id >= nextId) {
			nextId = id + 1;
		}
	}

	// 全IDの確認が終わってから、保持している物体を置き換える
	objects_.swap(objects);
	nextObjectId_ = nextId;
}
