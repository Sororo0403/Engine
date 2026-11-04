#include "scene/SceneRenderer.h"

#include "core/Logger.h"
#include "graphics/model/ModelManager.h"
#include "graphics/render/RenderSystem.h"
#include "graphics/render/RenderTexture.h"
#include "graphics/render/RenderWorld.h"
#include "scene/Scene.h"

#include <algorithm>
#include <exception>
#include <format>
#include <utility>
#include <vector>

SceneRenderer::SceneRenderer(const Scene &scene, ModelManager &models,
                             RenderSystem &render, ModelLoadFailure failure)
    : scene_(scene), models_(models), render_(render),
      modelLoadFailure_(failure) {
}

SceneRenderer::~SceneRenderer() = default;

bool SceneRenderer::SetLighting(const LightingSettings &settings) {
	if (!world_ || !settings.IsValid()) {
		return false;
	}
	const auto &direction = settings.direction;
	const double length = std::sqrt(double(direction.x) * direction.x +
	                                double(direction.y) * direction.y +
	                                double(direction.z) * direction.z);
	auto &light = world_->GetLight();
	light.direction = {float(direction.x / length), float(direction.y / length),
	                   float(direction.z / length)};
	light.intensity = settings.intensity;
	light.color = settings.color;
	light.ambient = settings.ambient;
	return true;
}

bool SceneRenderer::Initialize() {
	if (world_) {
		Logger::Error("SceneRenderer::Initialize: 初期化済みです。");
		return false;
	}

	world_ = render_.CreateWorld();

	if (!world_) {
		Logger::Error("SceneRenderer: 描画ワールドの作成に失敗しました。");
		return false;
	}

	return true;
}

bool SceneRenderer::Update(int width, int height) {
	if (!world_) {
		Logger::Error("SceneRenderer::Update: 未初期化です。");
		return false;
	}

	try {
		if (!SynchronizeObjects()) {
			return false;
		}

		if (width == 0 && height == 0) {
			return render_.Update(*world_);
		}

		return render_.Update(*world_, width, height);
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "SceneRendererの更新中に例外が発生しました: {}", error.what()));

		return false;
	}
}

void SceneRenderer::Draw(RenderTexture *target) const {
	if (!world_) {
		Logger::Error("SceneRenderer::Draw: 未初期化です。");
		return;
	}

	if (target) {
		const float clearColor[4] = {
		    0.1f,
		    0.1f,
		    0.1f,
		    1.0f,
		};

		render_.Draw(*world_, *target, clearColor);
	} else {
		render_.Draw(*world_);
	}
}

bool SceneRenderer::SynchronizeObjects() {
	const auto revision = models_.GetReloadRevision();
	const bool modelsChanged = revision != modelRevision_;

	// 削除・非表示・モデルなしの物体を描画ワールドから取り除く
	for (auto it = objects_.begin(); it != objects_.end();) {
		const SceneObject *object = scene_.FindObject(it->first);

		if (!object || !object->IsRenderable()) {
			world_->DestroyObject(it->second.handle);
			it = objects_.erase(it);
		} else {
			++it;
		}
	}

	for (const auto &[id, object] : scene_.GetObjects()) {
		if (!object.IsRenderable()) {
			continue;
		}

		// 描画物体を作る前に、対応情報の格納先を確保する
		auto it = objects_.try_emplace(id).first;
		Object &renderObject = it->second;

		if (renderObject.unavailable &&
		    renderObject.modelPath == object.mesh->modelPath &&
		    renderObject.failedCacheRevision == models_.GetCacheRevision()) {
			continue;
		}

		bool needsCreate = !world_->Contains(renderObject.handle) ||
		                   renderObject.modelPath != object.mesh->modelPath;
		ModelHandle model;

		if (needsCreate || modelsChanged) {
			model = models_.Load(object.mesh->modelPath);
			if (!model && !needsCreate) {
				// 他のモデルの更新時にも、以前の有効な描画を保持する。
				model = renderObject.model;
			}

			if (!model) {
				if (modelLoadFailure_ == ModelLoadFailure::SKIP) {
					// 参照とシーンの値は保持し、古い形状を描き続けない。
					auto path = object.mesh->modelPath;
					world_->DestroyObject(renderObject.handle);
					renderObject.modelPath = std::move(path);
					renderObject.handle = {};
					renderObject.model = {};
					renderObject.unavailable = true;
					renderObject.failedCacheRevision =
					    models_.GetCacheRevision();
					continue;
				}

				Logger::Error(std::format(
				    "SceneRenderer: 物体{}のモデル読み込みに失敗しました。",
				    id));
				return false;
			}

			needsCreate |= renderObject.model != model;
		}

		if (needsCreate) {
			std::filesystem::path modelPath = object.mesh->modelPath;

			const auto handle = world_->CreateObject(model);

			if (!world_->Contains(handle)) {
				Logger::Error(std::format(
				    "SceneRenderer: 物体{}の描画物体作成に失敗しました。", id));
				return false;
			}

			// 新しい描画物体の作成に成功してから、以前の物体を削除する
			world_->DestroyObject(renderObject.handle);

			renderObject.modelPath = std::move(modelPath);
			renderObject.handle = handle;
			renderObject.model = model;
			renderObject.unavailable = false;
		}

		for (std::size_t slot = 0;
		     slot < world_->GetMaterialCount(renderObject.handle); ++slot) {
			const auto found = std::find_if(
			    object.meshRenderer->textures.slots.begin(),
			    object.meshRenderer->textures.slots.end(),
			    [slot](const auto &binding) { return binding.first == slot; });
			auto material =
			    found == object.meshRenderer->textures.slots.end()
			        ? render_.GetMaterials().LoadTexture({})
			        : render_.GetMaterials().LoadTexture(found->second);

			if (!material) {
				material = render_.GetMaterials().LoadTexture({});
			}

			if (!material) {
				return false;
			}

			if (!world_->SetMaterial(renderObject.handle, slot, material)) {
				return false;
			}
		}

		// シーン側の変形情報を描画側へ反映する
		world_->GetTransform(renderObject.handle) = object.transform;

		if (!world_->SetColorTint(renderObject.handle,
		                          object.colorTint.value_or(ColorTint{}))) {
			Logger::Error(
			    std::format("SceneRenderer: 物体{}の色の設定が無効です。", id));
			return false;
		}
	}

	modelRevision_ = revision;
	return true;
}

void SceneRenderer::Finalize() {
	// GPUの利用完了後に描画ワールドを解放する
	world_.reset();
	objects_.clear();
	modelRevision_ = 0;
}

bool SceneRenderer::IsModelUnavailable(SceneObjectId id) const {
	const auto *object = scene_.FindObject(id);
	const auto found = objects_.find(id);
	return object && object->IsRenderable() && found != objects_.end() &&
	       found->second.unavailable &&
	       found->second.modelPath == object->mesh->modelPath;
}

bool SceneRenderer::SetCameraPose(const CameraPose &pose) {
	if (!world_) {
		Logger::Error("SceneRenderer::SetCameraPose: 未初期化です。");
		return false;
	}

	return world_->SetCameraPose(pose);
}

SceneObjectId
SceneRenderer::Pick(float imageX, float imageY, float *distance,
                    std::span<const SceneObjectId> excluded) const {
	if (!world_) {
		return 0;
	}

	std::vector<RenderObjectHandle> excludedHandles;
	excludedHandles.reserve(excluded.size());

	for (const auto id : excluded) {
		if (const auto it = objects_.find(id); it != objects_.end()) {
			excludedHandles.push_back(it->second.handle);
		}
	}

	float hitDistance = 0;
	const auto handle =
	    world_->Pick(imageX, imageY, &hitDistance, excludedHandles);

	if (!world_->Contains(handle)) {
		return 0;
	}

	for (const auto &[id, object] : objects_) {
		if (object.handle == handle) {
			if (distance) {
				*distance = hitDistance;
			}

			return id;
		}
	}

	return 0;
}

bool SceneRenderer::CreatePickingRay(float imageX, float imageY,
                                     Ray &ray) const {
	return world_ && world_->CreatePickingRay(imageX, imageY, ray);
}

bool SceneRenderer::GetCameraMatrices(DirectX::XMFLOAT4X4 &view,
                                      DirectX::XMFLOAT4X4 &projection) const {
	return world_ && world_->GetCameraMatrices(view, projection);
}

bool SceneRenderer::GetObjectBounds(
    SceneObjectId objectId, std::array<DirectX::XMFLOAT3, 8> &corners) const {
	const auto *object = scene_.FindObject(objectId);

	if (!world_ || !object || !object->IsRenderable()) {
		return false;
	}

	const auto it = objects_.find(objectId);

	if (it == objects_.end()) {
		return false;
	}

	return world_->GetObjectBounds(it->second.handle, corners);
}

bool SceneRenderer::AddDebugLine(const DebugLine &line) {
	return world_ && world_->AddDebugLine(line);
}

bool SceneRenderer::UpdateDebugLines() {
	if (!world_) {
		Logger::Error("SceneRenderer::UpdateDebugLines: 未初期化です。");
		return false;
	}

	try {
		return world_->UpdateDebugLines();
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("線の更新中に例外が発生しました: {}", error.what()));
		return false;
	}
}

bool SceneRenderer::SetCameraSettings(const CameraSettings &settings) {
	return world_ && world_->SetCameraSettings(settings);
}
