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
