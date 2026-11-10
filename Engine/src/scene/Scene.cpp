#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"

#include "math/TransformUtils.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

SceneObjectId Scene::CreateObject(std::string name,
                                  std::filesystem::path modelPath) {
	SceneObject object;
	object.name = std::move(name);

	if (!modelPath.empty()) {
		object.mesh = MeshComponent{std::move(modelPath)};
		object.meshRenderer = MeshRendererComponent{};
	}

	return CreateObject(std::move(object));
}

SceneObjectId Scene::CreateObject(SceneObject object) {
	if (nextObjectId_ == (std::numeric_limits<SceneObjectId>::max)()) {
		throw std::overflow_error("Scene: 物体番号の上限に達しました。");
	}

	if (!TransformUtils::IsValid(object.transform)) {
		throw std::invalid_argument(
		    "Scene::CreateObject: 変形情報が無効です。");
	}

	if (object.parentId != 0 && !Contains(object.parentId)) {
		throw std::invalid_argument(
		    "Scene::CreateObject: 親の物体が存在しません。");
	}

	if (object.name.empty()) {
		object.name = "Object";
	}

	const SceneObjectId id = nextObjectId_;

	// 全データの格納に成功してから次の番号へ進める
	const bool inserted = objects_.emplace(id, std::move(object)).second;

	if (!inserted) {
		throw std::logic_error(
		    "Scene::CreateObject: 物体のIDが重複しています。");
	}

	++nextObjectId_;
	return id;
}

bool Scene::DestroyObject(SceneObjectId id) {
	if (id == 0 || !SceneHierarchy::GetChildren(*this, id).empty()) {
		return false;
	}

	return objects_.erase(id) != 0;
}

bool Scene::Contains(SceneObjectId id) const {
	return objects_.contains(id);
}

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

void Scene::Clear() {
	objects_.clear();
	lighting_.reset();
	fog_ = {};
	monochrome_ = {};
	monochromeForeground_ = 0;
	backgroundFreeze_.reset();
	legacyDirectionalLight_ = false;
}

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

	Scene candidate;
	candidate.objects_ = std::move(objects);
	std::vector<SceneObjectId> order;

	if (!SceneHierarchy::GetEvaluationOrder(candidate, order)) {
		throw std::invalid_argument(
		    "Scene::ReplaceObjects: 親子関係が無効です。");
	}

	// 全IDと親子関係の確認が終わってから、保持している物体を置き換える
	objects_.swap(candidate.objects_);
	nextObjectId_ = nextId;
}

bool Scene::RestoreObject(SceneObjectId id, const SceneObject &object) {
	if (id == 0 || id >= nextObjectId_ || objects_.contains(id) ||
	    (object.parentId != 0 && !Contains(object.parentId))) {
		return false;
	}

	return objects_.emplace(id, object).second;
}

bool Scene::ApplyTransaction(const std::function<bool(Scene &)> &operation) {
	if (!operation) {
		return false;
	}

	Scene working;

	working.objects_ = objects_;
	working.lighting_ = lighting_;
	working.fog_ = fog_;
	working.monochrome_ = monochrome_;
	working.monochromeForeground_ = monochromeForeground_;
	working.backgroundFreeze_ = backgroundFreeze_;
	working.legacyDirectionalLight_ = legacyDirectionalLight_;

	// 削除済み物体のIDも復元できるよう、発行済み番号を引き継ぐ
	working.nextObjectId_ = nextObjectId_;

	if (!operation(working)) {
		return false;
	}

	std::vector<SceneObjectId> order;

	if (!SceneHierarchy::GetEvaluationOrder(working, order)) {
		return false;
	}

	// 全操作が成功してから、保持するデータを置き換える
	objects_.swap(working.objects_);
	lighting_.swap(working.lighting_);
	monochrome_ = working.monochrome_;
	monochromeForeground_ = working.monochromeForeground_;
	backgroundFreeze_.swap(working.backgroundFreeze_);
	legacyDirectionalLight_ = working.legacyDirectionalLight_;
	nextObjectId_ = working.nextObjectId_;

	return true;
}

void Scene::CopyFrom(const Scene &source) {
	Scene candidate;
	candidate.ReplaceObjects(source.objects_);
	const auto next = (std::max)(nextObjectId_, source.nextObjectId_);
	objects_.swap(candidate.objects_);
	lighting_ = source.lighting_;
	fog_ = source.fog_;
	monochrome_ = source.monochrome_;
	monochromeForeground_ = source.monochromeForeground_;
	backgroundFreeze_ = source.backgroundFreeze_;
	legacyDirectionalLight_ = source.legacyDirectionalLight_;
	nextObjectId_ = next;
}

bool Scene::SetLighting(std::optional<LightingSettings> lighting) {
	if (lighting && !lighting->IsValid()) {
		return false;
	}

	lighting_ = lighting;
	return true;
}

bool Scene::SetFog(const FogSettings &fog) {
	if (!fog.IsValid()) {
		return false;
	}

	fog_ = fog;
	return true;
}

bool Scene::SetMonochrome(const MonochromeEffect &effect, SceneObjectId foreground,
                          bool freezeBackground) {
	if (!effect.IsValid() || (foreground && !Contains(foreground)))
		return false;
	if (!freezeBackground)
		backgroundFreeze_.reset();
	else if (!backgroundFreeze_ || foreground != monochromeForeground_)
		backgroundFreeze_ = std::make_shared<SceneBackgroundFreeze>();
	monochrome_ = effect;
	monochromeForeground_ = foreground;
	return true;
}

void Scene::Swap(Scene &other) noexcept {
	objects_.swap(other.objects_);
	lighting_.swap(other.lighting_);
	std::swap(fog_, other.fog_);
	std::swap(monochrome_, other.monochrome_);
	std::swap(monochromeForeground_, other.monochromeForeground_);
	backgroundFreeze_.swap(other.backgroundFreeze_);
	std::swap(legacyDirectionalLight_, other.legacyDirectionalLight_);
	std::swap(nextObjectId_, other.nextObjectId_);
}

void Scene::AdoptValidated(Scene &candidate) noexcept {
	const auto next = (std::max)(nextObjectId_, candidate.nextObjectId_);
	Swap(candidate);
	nextObjectId_ = next;
}
