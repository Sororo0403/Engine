#include "scene/SceneRenderer.h"
#include "audio/AudioClipManager.h"
#include "graphics/render/ParticleRenderer.h"
#include "ui/CanvasLayout.h"
#include <stdexcept>

#include "core/Logger.h"
#include "graphics/model/ModelManager.h"
#include "graphics/render/RenderSystem.h"
#include "graphics/render/RenderTexture.h"
#include "graphics/render/RenderWorld.h"
#include "graphics/ui/CanvasRenderer.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneLighting.h"

#include <algorithm>
#include <exception>
#include <format>
#include <set>
#include <utility>
#include <vector>

SceneRenderer::SceneRenderer(const Scene &scene, ModelManager &models,
                             RenderSystem &render, ModelLoadFailure failure)
    : scene_(scene), models_(models), render_(render),
      modelLoadFailure_(failure) {
}

SceneRenderer::~SceneRenderer() = default;

bool SceneRenderer::SetLighting(const LightingSettings &fallback) {
	if (!world_ || !fallback.IsValid())
		return false;

	fallbackLighting_ = fallback;
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

	canvas_ = render_.CreateCanvas(modelLoadFailure_ == ModelLoadFailure::SKIP);

	if (!canvas_) {
		world_.reset();
		return false;
	}

	particles_ = render_.CreateParticles();
	return bool(particles_);
}

bool SceneRenderer::PreparePrefabResources(
    std::span<const std::shared_ptr<const Scene>> prefabs,
    AudioClipManager &clips, std::string &error) {
	try {
		if (!canvas_)
			throw std::runtime_error(
			    "Initialize the renderer before preparing Prefabs.");
		auto next = preparedPrefabs_;
		std::vector<std::filesystem::path> images, particleImages;

		for (const auto &scene : prefabs) {
			if (!scene)
				throw std::runtime_error("Prefab scene is missing.");
			if (std::ranges::find(next, scene) != next.end())
				continue;
			for (const auto &[id, o] : scene->GetObjects()) {
				(void)id;
				if (o.HasMesh() && !models_.Load(o.mesh->modelPath))
					throw std::runtime_error("Could not prepare Prefab model.");
				if (o.meshRenderer)
					for (const auto &[slot, path] :
					     o.meshRenderer->textures.slots) {
						(void)slot;
						if (!render_.GetMaterials().LoadTexture(path))
							throw std::runtime_error(
							    "Could not prepare Prefab material.");
					}
				if (o.audioSource && !o.audioSource->clipPath.empty()) {
					const auto clip =
					    clips.Load(o.audioSource->clipPath, error);
					if (!clip)
						throw std::runtime_error(error);
					if (o.audioSource->spatial && clip.GetChannels() != 1)
						throw std::runtime_error(
						    "Spatial Prefab audio requires mono WAV.");
				}

				if (o.particleEmitter && !o.particleEmitter->texture.empty())
					particleImages.push_back(o.particleEmitter->texture);
				if (o.image && !o.image->texture.empty())
					images.push_back(o.image->texture);
			}

			next.push_back(scene);
		}

		if (!canvas_->PrepareImages(images, error) ||
		    !particles_->PrepareImages(particleImages, error))
			return false;
		preparedPrefabs_.assign(prefabs.begin(), prefabs.end());
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

void SceneRenderer::ClearPrefabResources() {
	preparedPrefabs_.clear();
	backgroundFreeze_.reset();
	frozenParticles_.clear();
	frozenForeground_.clear();
	particleBatches_.clear();
	animatorPoses_.clear();
	if (particles_)
		particles_->Clear();
	if (canvas_)
		canvas_->ClearPreparedImages();
}

bool SceneRenderer::Update(int width, int height) {
	if (!world_ || !particles_) {
		Logger::Error("SceneRenderer is not initialized.");
		return false;
	}

	try {
		const auto &freeze = scene_.GetBackgroundFreeze();
		const bool keepBackground = freeze && freeze == backgroundFreeze_;
		if (!keepBackground) {
			if (freeze)
				frozenParticles_ = particleBatches_;
			else
				frozenParticles_.clear();
		}
		const auto &batches = freeze ? frozenParticles_ : particleBatches_;
		std::vector<std::filesystem::path> images;

		for (const auto &[id, o] : scene_.GetObjects()) {
			(void)id;
			if (o.particleEmitter && !o.particleEmitter->texture.empty())
				images.push_back(o.particleEmitter->texture);
		}
		for (const auto &batch : batches) {
			if (!batch.texture.empty())
				images.push_back(batch.texture);
		}

		std::string error;

		if (modelLoadFailure_ != ModelLoadFailure::SKIP &&
		    !particles_->PrepareImages(images, error)) {
			Logger::Error(error);
			return false;
		}

		if (!SynchronizeObjects(keepBackground))
			return false;
		world_->SetFog(scene_.GetFog());
		const bool updated = width == 0 && height == 0
		                         ? render_.Update(*world_)
		                         : render_.Update(*world_, width, height);
		if (!updated)
			return false;
		DirectX::XMFLOAT4X4 view, projection;

		if (!world_->GetCameraMatrices(view, projection) ||
		    !particles_->Update(batches, view, projection, error)) {
			Logger::Error(error);
			return false;
		}

		if (screenUIEnabled_ &&
		    !render_.UpdateCanvas(*canvas_, scene_, width, height, buttonVisuals_))
			return false;
		backgroundFreeze_ = freeze;
		return true;
	} catch (const std::exception &error) {
		Logger::Error(std::format("SceneRenderer update: {}", error.what()));
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

		render_.Draw(*world_, *target, clearColor,
		             screenUIEnabled_ ? canvas_.get() : nullptr,
		             particles_.get());
	} else {
		render_.Draw(*world_, screenUIEnabled_ ? canvas_.get() : nullptr,
		             particles_.get());
	}
}

bool SceneRenderer::SynchronizeObjects(bool keepBackground) {
	SceneHierarchy::WorldTransforms transforms;

	if (!SceneHierarchy::BuildWorldTransforms(scene_, transforms)) {
		Logger::Error("SceneRenderer: 親子関係またはワールド変形が無効です。");
		return false;
	}

	SceneActivity::States active;

	if (!SceneActivity::BuildStates(scene_, active)) {
		return false;
	}

	const auto &monochrome = scene_.GetMonochrome();
	const auto foreground = scene_.GetMonochromeForeground();
	std::vector<SceneObjectId> coloredObjects;
	const bool freezing = static_cast<bool>(scene_.GetBackgroundFreeze());
	if ((freezing || monochrome.amount > 0) && foreground &&
	    scene_.Contains(foreground) &&
	    !SceneHierarchy::GetSubtree(scene_, foreground, coloredObjects))
		return false;
	std::ranges::sort(coloredObjects);
	if (!keepBackground)
		frozenForeground_ = freezing ? coloredObjects : std::vector<SceneObjectId>{};
	const auto isForeground = [&](SceneObjectId id) {
		return std::ranges::binary_search(coloredObjects, id);
	};

	if (!keepBackground) {
		std::string lightError;
		DirectionalLightsData directionalLights;
		ShadowSettings shadows;

		if (!SceneLighting::CollectDirectionalLights(scene_, transforms, active,
		                                             fallbackLighting_, directionalLights,
		                                             shadows, lightError) ||
		    !world_->SetShadows(shadows)) {
			Logger::Error(lightError.empty() ? "Directional Light update failed."
			                                 : lightError);
			return false;
		}
		world_->SetDirectionalLights(directionalLights);

		std::vector<PointLightData> lights;

		if (!SceneLighting::CollectPointLights(scene_, lights, lightError) ||
		    !world_->SetPointLights(lights)) {
			Logger::Error(lightError.empty() ? "Point Light update failed."
			                                 : lightError);
			return false;
		}

		std::vector<SpotLightData> spotLights;

		if (!SceneLighting::CollectSpotLights(scene_, spotLights, lightError) ||
		    !world_->SetSpotLights(spotLights)) {
			Logger::Error(lightError.empty() ? "Spot Light update failed."
			                                 : lightError);
			return false;
		}
	}

	const auto revision = models_.GetReloadRevision();
	const bool modelsChanged = revision != modelRevision_;

	// 削除・非表示・モデルなしの物体を描画ワールドから取り除く
	for (auto it = objects_.begin(); it != objects_.end();) {
		if (keepBackground && !isForeground(it->first) &&
		    !std::ranges::binary_search(frozenForeground_, it->first)) {
			const auto handle = it->second.handle;
			if (world_->Contains(handle))
				for (std::size_t slot = 0; slot < world_->GetMaterialCount(handle); ++slot)
					world_->GetMaterial(handle, slot).monochrome = monochrome;
			++it;
			continue;
		}
		const SceneObject *object = scene_.FindObject(it->first);

		if (!object || !active.at(it->first) || !object->IsRenderable() ||
		    (keepBackground && !isForeground(it->first))) {
			world_->DestroyObject(it->second.handle);
			it = objects_.erase(it);
		} else {
			++it;
		}
	}

	for (const auto &[id, object] : scene_.GetObjects()) {
		if (keepBackground && !isForeground(id))
			continue;
		if (!active.at(id) || !object.IsRenderable()) {
			continue;
		}

		if (!object.meshRenderer->IsValid()) {
			Logger::Error(
			    "SceneRenderer: Mesh Rendererの画像またはUV設定が無効です。");
			return false;
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
			        ? MaterialHandle{}
			        : render_.GetMaterials().LoadTexture(found->second);

			// 上書きがない場合は、モデルの既定の材質へ戻す。
			if (!world_->SetMaterial(renderObject.handle, slot, material)) {
				return false;
			}

			auto &data = world_->GetMaterial(renderObject.handle, slot);
			data.uvScale = {object.meshRenderer->uvScale[0],
			                object.meshRenderer->uvScale[1]};
			data.uvOffset = {object.meshRenderer->uvOffset[0],
			                 object.meshRenderer->uvOffset[1]};
			data.reveal = object.meshRenderer->reveal;
			data.receiveFog = object.meshRenderer->receiveFog ? 1 : 0;
			data.monochrome = isForeground(id) ? MonochromeEffect{} : monochrome;
		}

		const auto animator = animatorPoses_.find(id);
		const ModelAnimationPose *skin = nullptr;

		if (object.animator && animator != animatorPoses_.end()) {
			if (animator->second.model !=
			    models_.GetAnimationData(renderObject.model)) {
				Logger::Error("Animator model changed while playing. Restart "
				              "Play to reload its clips.");
				return false;
			}

			skin = animator->second.pose.get();
		}

		if (!world_->SetAnimationPose(renderObject.handle, skin))
			return false;
		if (!world_->SetShadowCaster(renderObject.handle, !isForeground(id)))
			return false;
		// 親子の合成結果を、描画・選択・外接ボックスで共通に使用する。
		const bool transformed =
		    object.parentId == 0
		        ? world_->SetTransform(renderObject.handle, object.transform)
		        : world_->SetWorldMatrix(renderObject.handle,
		                                 transforms.at(id).matrix);

		if (!transformed) {
			return false;
		}

		if (!world_->SetColorTint(renderObject.handle,
		                          object.colorTint.value_or(ColorTint{}))) {
			Logger::Error(
			    std::format("SceneRenderer: 物体{}の色の設定が無効です。", id));
			return false;
		}
	}

	if (!keepBackground)
		modelRevision_ = revision;
	return true;
}

void SceneRenderer::Finalize() {
	// GPUの利用完了後に描画ワールドを解放する
	world_.reset();
	particles_.reset();
	particleBatches_.clear();
	frozenParticles_.clear();
	backgroundFreeze_.reset();
	frozenForeground_.clear();
	animatorPoses_.clear();
	canvas_.reset();
	preparedPrefabs_.clear();
	particleBatches_.clear();
	animatorPoses_.clear();
	if (particles_)
		particles_->Clear();
	buttonVisuals_.clear();
	objects_.clear();
	modelRevision_ = 0;
}

bool SceneRenderer::IsModelUnavailable(SceneObjectId id) const {
	const auto *object = scene_.FindObject(id);
	const auto found = objects_.find(id);
	return object && SceneActivity::IsActive(scene_, id) &&
	       object->IsRenderable() && found != objects_.end() &&
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

	if (!world_ || !object || !SceneActivity::IsActive(scene_, objectId) ||
	    !object->IsRenderable()) {
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

bool SceneRenderer::SetUIButtonVisuals(
    std::span<const UIButtonVisual> visuals) {
	if (visuals.size() > CanvasLayout::kMaximumElements)
		return false;
	std::set<SceneObjectId> seen;

	for (const auto &visual : visuals) {
		const auto *object = scene_.FindObject(visual.object);
		if (!object || !object->button || !seen.insert(visual.object).second ||
		    visual.state < UIButtonVisual::State::NORMAL ||
		    visual.state > UIButtonVisual::State::DISABLED)
			return false;
	}

	std::vector<UIButtonVisual> candidate(visuals.begin(), visuals.end());
	buttonVisuals_.swap(candidate);
	return true;
}

void SceneRenderer::SetParticles(std::vector<ParticleDrawBatch> batches) {
	particleBatches_ = std::move(batches);
}

bool SceneRenderer::SetAnimatorPoses(std::vector<AnimatorDrawPose> poses) {
	std::map<SceneObjectId, AnimatorDrawPose> next;

	for (auto &pose : poses) {
		const auto *object = scene_.FindObject(pose.object);
		if (!object || !object->animator || !pose.model || !pose.pose ||
		    !next.emplace(pose.object, std::move(pose)).second)
			return false;
	}

	animatorPoses_ = std::move(next);
	return true;
}
