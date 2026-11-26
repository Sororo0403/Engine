#include "selection/EditorClipboard.h"

#include "math/TransformUtils.h"
#include "physics/ColliderUtils.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include "scene/SceneAssetReferences.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneObjectReferences.h"

#include <set>
#include <system_error>

void EditorClipboard::RewriteAssets(const std::function<void(SceneObject &)> &rewrite) {
	for (auto &object : objects_) {
		rewrite(object);
	}
}

std::filesystem::path EditorClipboard::FindUnavailableAsset() const {
	std::filesystem::path unavailable;
	const auto check = [&](const std::filesystem::path &path) {
		if (path.empty() || !unavailable.empty()) {
			return;
		}
		const auto absolute = PathUtils::ResolveFromExecutable(path);
		std::error_code status;
		if (!std::filesystem::is_regular_file(absolute, status)) {
			unavailable = absolute;
		}
	};
	for (const auto &object : objects_) {
		SceneAssetReferences::Visit(object, check);
		if (object.prefabLink) {
			check(object.prefabLink->asset);
		}
		if (!unavailable.empty()) {
			break;
		}
	}
	return unavailable;
}

bool EditorClipboard::Copy(const Scene &scene,
                           std::span<const SceneObjectId> objectIds) {
	if (objectIds.empty()) {
		return false;
	}

	std::vector<SceneObjectId> sourceIds;
	if (!SceneHierarchy::ExpandSelection(scene, objectIds, sourceIds))
		return false;
	const std::set<SceneObjectId> seen(sourceIds.begin(), sourceIds.end());
	std::vector<SceneObject> objects;
	objects.reserve(sourceIds.size());

	for (const auto id : sourceIds) {
		auto object = *scene.FindObject(id);

		if (object.prefabLink && !seen.contains(object.prefabLink->root)) {
			object.prefabLink.reset();
		}

		auto parent = object.GetParentId();
		if (parent && !seen.contains(parent)) {
			SceneWorldTransform world;
			if (!SceneHierarchy::GetWorldTransform(scene, id, world) ||
			    !TransformUtils::TryFromWorldMatrix(world.matrix,
			                                        object.transform))
				return false;
			parent = 0;
		}

		if (!TransformUtils::IsValid(object.transform))
			return false;
		objects.emplace_back(std::move(object), parent);
	}

	std::vector<std::size_t> externalReferences;

	for (std::size_t i = 0; i < objects.size(); ++i) {
		if (SceneObjectReferences::HasExternal(objects[i], sourceIds)) {
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
		SceneObjectReferences::ClearExternal(objects_[index], sourceIds_);
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

bool EditorClipboard::CopyCollider(const Collider &collider) {
	if (!ColliderUtils::IsValid(collider)) {
		return false;
	}

	collider_ = collider;
	return true;
}

const Collider *EditorClipboard::GetCollider() const {
	return collider_ ? &*collider_ : nullptr;
}

bool EditorClipboard::CopyTextureUVs(const std::array<float, 2> &scale,
                                     const std::array<float, 2> &offset) {
	MeshRendererComponent values;
	values.uvScale = scale;
	values.uvOffset = offset;

	if (!values.IsValid()) {
		return false;
	}

	textureUVs_ = TextureUVs{values.uvScale, values.uvOffset};
	return true;
}

const EditorClipboard::TextureUVs *EditorClipboard::GetTextureUVs() const {
	return textureUVs_ ? &*textureUVs_ : nullptr;
}

void EditorClipboard::Clear() {
	objects_.clear();
	sourceIds_.clear();
	externalReferences_.clear();
	transform_.reset();
	collider_.reset();
	textureUVs_.reset();
}
