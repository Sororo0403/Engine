#include "scene/SceneRenderer.h"

#include "core/Logger.h"
#include "graphics/model/ModelManager.h"
#include "graphics/render/RenderSystem.h"
#include "graphics/render/RenderTexture.h"
#include "graphics/render/RenderWorld.h"
#include "scene/Scene.h"

#include <exception>
#include <format>
#include <utility>

SceneRenderer::SceneRenderer(const Scene &scene, ModelManager &models,
                             RenderSystem &render)
    : scene_(scene), models_(models), render_(render) {}

SceneRenderer::~SceneRenderer() = default;

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
	// 削除された物体、またはモデルを持たなくなった物体を取り除く
	for (auto it = objects_.begin(); it != objects_.end();) {
		const SceneObject *object = scene_.FindObject(it->first);

		if (!object || object->modelPath.empty()) {
			world_->DestroyObject(it->second.handle);
			it = objects_.erase(it);
		} else {
			++it;
		}
	}

	for (const auto &[id, object] : scene_.GetObjects()) {
		if (object.modelPath.empty()) {
			continue;
		}

		// 描画物体を作る前に、対応情報の格納先を確保する
		auto it = objects_.try_emplace(id).first;
		Object &renderObject = it->second;

		const bool needsCreate = !world_->Contains(renderObject.handle) ||
		                         renderObject.modelPath != object.modelPath;

		if (needsCreate) {
			// パスのコピーはGPUリソースを作成する前に済ませる
			std::filesystem::path modelPath = object.modelPath;

			const auto model = models_.Load(modelPath);

			if (!model) {
				Logger::Error(std::format(
				    "SceneRenderer: 物体{}のモデル読み込みに失敗しました。",
				    id));
				return false;
			}

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
		}

		// シーン側の変形情報を描画側へ反映する
		world_->GetTransform(renderObject.handle) = object.transform;
	}

	return true;
}

void SceneRenderer::Finalize() {
	// GPUの利用完了後に描画ワールドを解放する
	world_.reset();
	objects_.clear();
}

bool SceneRenderer::SetCameraPose(const CameraPose &pose) {
	if (!world_) {
		Logger::Error("SceneRenderer::SetCameraPose: 未初期化です。");
		return false;
	}

	return world_->SetCameraPose(pose);
}

SceneObjectId SceneRenderer::Pick(float imageX, float imageY) const {
	if (!world_) {
		return 0;
	}

	const auto handle = world_->Pick(imageX, imageY);

	if (!world_->Contains(handle)) {
		return 0;
	}

	for (const auto &[id, object] : objects_) {
		if (object.handle == handle) {
			return id;
		}
	}

	return 0;
}

bool SceneRenderer::GetCameraMatrices(DirectX::XMFLOAT4X4 &view,
                                      DirectX::XMFLOAT4X4 &projection) const {
	return world_ && world_->GetCameraMatrices(view, projection);
}

bool SceneRenderer::GetObjectBounds(
    SceneObjectId objectId, std::array<DirectX::XMFLOAT3, 8> &corners) const {
	if (!world_ || !scene_.Contains(objectId)) {
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
