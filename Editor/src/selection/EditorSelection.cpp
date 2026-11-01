#include "selection/EditorSelection.h"

#include "scene/Scene.h"

#include <algorithm>

bool EditorSelection::SelectOnly(SceneObjectId objectId) {
	if (objectId == 0) {
		return Clear();
	}

	if (ids_.size() == 1 && ids_.front() == objectId) {
		return false;
	}

	if (ids_.empty()) {
		ids_.push_back(objectId);
	} else {
		ids_.front() = objectId;
		ids_.resize(1);
	}

	return true;
}

bool EditorSelection::Toggle(SceneObjectId objectId) {
	if (objectId == 0) {
		return false;
	}

	if (Contains(objectId)) {
		return Remove(objectId);
	}

	ids_.push_back(objectId);
	return true;
}

bool EditorSelection::Remove(SceneObjectId objectId) {
	return std::erase(ids_, objectId) != 0;
}

bool EditorSelection::RemoveMissing(const Scene &scene) {
	return std::erase_if(ids_, [&scene](SceneObjectId id) {
		       return !scene.Contains(id);
	       }) != 0;
}

bool EditorSelection::Clear() {
	const bool changed = !ids_.empty();

	ids_.clear();
	return changed;
}

bool EditorSelection::Contains(SceneObjectId objectId) const {
	return std::find(ids_.begin(), ids_.end(), objectId) != ids_.end();
}

std::size_t EditorSelection::GetCount() const { return ids_.size(); }

SceneObjectId EditorSelection::GetActiveId() const {
	return ids_.empty() ? 0 : ids_.back();
}

std::span<const SceneObjectId> EditorSelection::GetIds() const {
	return {ids_.data(), ids_.size()};
}
