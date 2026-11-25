#include "selection/EditorClipboard.h"

#include "math/MathUtils.h"
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

		if (object.parentId != 0 && !seen.contains(object.parentId)) {
			SceneWorldTransform world;
			if (!SceneHierarchy::GetWorldTransform(scene, id, world) ||
			    !TransformUtils::TryFromWorldMatrix(world.matrix,
			                                        object.transform))
				return false;
			object.parentId = 0;
		}

		if (!TransformUtils::IsValid(object.transform))
			return false;
		objects.push_back(std::move(object));
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
	return CopyTransform(transform, transform.translation);
}

bool EditorClipboard::CopyTransform(const Transform &transform,
                                    const DirectX::XMFLOAT3 &worldPosition) {
	if (!TransformUtils::IsValid(transform) ||
	    !MathUtils::IsFinite(worldPosition)) {
		return false;
	}

	transform_ = transform;
	worldPosition_ = worldPosition;
	return true;
}

const DirectX::XMFLOAT3 *EditorClipboard::GetWorldPosition() const {
	return worldPosition_ ? &*worldPosition_ : nullptr;
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

bool EditorClipboard::CopyFollowSettings(const FollowCameraSettings &settings) {
	if (!settings.IsValid()) {
		return false;
	}

	// 別シーンで同じIDが使われても、元の追従対象を参照させない。
	auto value = settings;
	value.target = 0;
	followSettings_ = value;
	return true;
}

const FollowCameraSettings *EditorClipboard::GetFollowSettings() const {
	return followSettings_ ? &*followSettings_ : nullptr;
}

bool EditorClipboard::CopyRespawnSettings(const RespawnSettings &settings) {
	if (!settings.IsValid()) {
		return false;
	}

	respawnSettings_ = settings;
	return true;
}

const RespawnSettings *EditorClipboard::GetRespawnSettings() const {
	return respawnSettings_ ? &*respawnSettings_ : nullptr;
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
	worldPosition_.reset();
	collider_.reset();
	characterSettings_.reset();
	followSettings_.reset();
	respawnSettings_.reset();
	textureUVs_.reset();
}
