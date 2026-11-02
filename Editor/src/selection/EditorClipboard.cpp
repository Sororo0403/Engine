#include "selection/EditorClipboard.h"

#include "math/TransformUtils.h"
#include "physics/BoxColliderUtils.h"
#include "scene/Scene.h"

#include <set>

bool EditorClipboard::Copy(const Scene &scene,
                           std::span<const SceneObjectId> objectIds) {
	if (objectIds.empty()) {
		return false;
	}

	std::set<SceneObjectId> seen;
	std::vector<SceneObject> objects;
	objects.reserve(objectIds.size());

	for (const SceneObjectId id : objectIds) {
		if (id == 0 || !seen.insert(id).second) {
			return false;
		}

		const SceneObject *object = scene.FindObject(id);

		if (!object || !TransformUtils::IsValid(object->transform)) {
			return false;
		}

		objects.push_back(*object);
	}

	std::vector<SceneObjectId> sourceIds(objectIds.begin(), objectIds.end());
	std::vector<std::size_t> externalReferences;

	for (std::size_t i = 0; i < objects.size(); ++i) {
		if (objects[i].followCamera && objects[i].followCamera->target != 0 &&
		    !seen.contains(objects[i].followCamera->target)) {
			externalReferences.push_back(i);
		}
	}

	objects_.swap(objects);
	sourceIds_.swap(sourceIds);
	externalReferences_.swap(externalReferences);
	return true;
}

std::span<const SceneObject> EditorClipboard::GetObjects() const {
	return objects_;
}

std::span<const SceneObjectId> EditorClipboard::GetSourceIds() const {
	return sourceIds_;
}

void EditorClipboard::DetachExternalReferences() noexcept {
	for (const auto index : externalReferences_) {
		if (objects_[index].followCamera) {
			objects_[index].followCamera->target = 0;
		}
	}

	externalReferences_.clear();
}

bool EditorClipboard::HasObjects() const {
	return !objects_.empty();
}

bool EditorClipboard::CopyTransform(const Transform &transform) {
	if (!TransformUtils::IsValid(transform)) {
		return false;
	}

	transform_ = transform;
	return true;
}

const Transform *EditorClipboard::GetTransform() const {
	return transform_ ? &*transform_ : nullptr;
}

bool EditorClipboard::CopyCollider(const BoxCollider &collider) {
	if (!BoxColliderUtils::IsValid(collider)) {
		return false;
	}

	collider_ = collider;
	return true;
}

const BoxCollider *EditorClipboard::GetCollider() const {
	return collider_ ? &*collider_ : nullptr;
}

bool EditorClipboard::CopyCameraLens(const CameraSettings &settings) {
	if (!settings.IsValid()) {
		return false;
	}

	cameraLens_ = settings;
	return true;
}

const CameraSettings *EditorClipboard::GetCameraLens() const {
	return cameraLens_ ? &*cameraLens_ : nullptr;
}

bool EditorClipboard::CopyCharacterSettings(const CharacterSettings &settings) {
	if (!settings.IsValid()) {
		return false;
	}

	characterSettings_ = settings;
	return true;
}

const CharacterSettings *EditorClipboard::GetCharacterSettings() const {
	return characterSettings_ ? &*characterSettings_ : nullptr;
}

void EditorClipboard::Clear() {
	objects_.clear();
	sourceIds_.clear();
	externalReferences_.clear();
	transform_.reset();
	collider_.reset();
	cameraLens_.reset();
	characterSettings_.reset();
}
