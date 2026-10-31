#include "scene/GameScene.h"
#include "core/Logger.h"

#include "core/EngineContext.h"
#include "math/Transform.h"
#include "graphics/debug/DebugUI.h"
#include "graphics/model/ModelManager.h"
#include "graphics/render/RenderSystem.h"
#include "graphics/render/RenderWorld.h"
#include "input/InputSystem.h"
#include "input/Key.h"

#include <DirectXMath.h>
#include <cmath>
#include <exception>
#include <format>

GameScene::GameScene(const EngineContext &context)
    : models_(context.models), render_(context.render), input_(context.input),
      debugUI_(context.debugUI) {}

GameScene::~GameScene() = default;

bool GameScene::Initialize() {
	if (initialized_) {
		Logger::Error("GameScene::Initialize: 初期化済みです。");
		return false;
	}

	try {
		world_ = render_.CreateWorld();

		if (!world_ || !CreateModel() || !CreateInstances()) {
			Finalize();
			return false;
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format("GameSceneの初期化中に例外が発生しました: {}",
		                          error.what()));

		Finalize();
		return false;
	}

	initialized_ = true;

	Logger::Info("GameSceneを初期化しました。");
	return true;
}

bool GameScene::CreateModel() {
	model_ = models_.Load(L"resources/game/models/box.glb");

	return static_cast<bool>(model_);
}

bool GameScene::CreateInstances() {
	for (std::size_t i = 0; i < objects_.size(); ++i) {
		objects_[i] = world_->CreateObject(model_);

		if (!world_->Contains(objects_[i])) {
			Logger::Error(
			    std::format("GameScene: 物体{}の作成に失敗しました。", i));
			return false;
		}

		Transform &transform = world_->GetTransform(objects_[i]);

		transform.scale = {0.45f, 0.45f, 0.45f};
		transform.rotation = {0.3f, 0.4f, 0.0f};
		transform.translation.x = (i == 0) ? -0.6f : 0.6f;
	}

	return true;
}

bool GameScene::Update(float deltaTime) {
	if (!initialized_) {
		Logger::Error("GameScene::Update: 未初期化です。");
		return false;
	}

	if (!std::isfinite(deltaTime) || deltaTime < 0.0f) {
		Logger::Error("GameScene::Update: 経過時間が無効です。");
		return false;
	}

#if defined(_DEBUG)
	DrawDebugControls();
#endif

	UpdateCamera(deltaTime);

	if (autoRotate_) {
		for (std::size_t i = 0; i < objects_.size(); ++i) {
			Transform &transform = world_->GetTransform(objects_[i]);

			const float rotationSpeed = (i == 0) ? 1.0f : -0.7f;

			transform.rotation.y += rotationSpeed * deltaTime;
		}
	}

	return render_.Update(*world_);
}

void GameScene::UpdateCamera(float deltaTime) {
	if (debugUI_.WantsKeyboard()) {
		return;
	}

	const Keyboard &keyboard = input_.GetKeyboard();

	float moveX = 0.0f;
	float moveZ = 0.0f;

	if (keyboard.IsDown(Key::W)) {
		moveZ += 1.0f;
	}

	if (keyboard.IsDown(Key::S)) {
		moveZ -= 1.0f;
	}

	if (keyboard.IsDown(Key::A)) {
		moveX -= 1.0f;
	}

	if (keyboard.IsDown(Key::D)) {
		moveX += 1.0f;
	}

	const float length = std::sqrt(moveX * moveX + moveZ * moveZ);

	if (length > 0.0f) {
		constexpr float moveSpeed = 2.0f;
		const float distance = moveSpeed * deltaTime;

		world_->MoveCamera({
		    moveX / length * distance,
		    0.0f,
		    moveZ / length * distance,
		});
	}
}

void GameScene::Draw() const {
	if (!initialized_) {
		Logger::Error("GameScene::Draw: 未初期化です。");
		return;
	}

	render_.Draw(*world_);
}

#if defined(_DEBUG)

void GameScene::DrawDebugControls() {
	if (!debugUI_.BeginWindow("Game Scene")) {
		debugUI_.EndWindow();
		return;
	}

	debugUI_.Checkbox("Auto rotate", autoRotate_);

	if (debugUI_.CollapsingHeader("Directional Light")) {
		auto &light = world_->GetLight();

		debugUI_.SliderFloat("Intensity", light.intensity, 0.0f, 5.0f);

		debugUI_.SliderFloat("Ambient", light.ambient, 0.0f, 1.0f);

		debugUI_.ColorEdit3("Light color", light.color);
	}

	for (std::size_t i = 0; i < objects_.size(); ++i) {
		debugUI_.PushID(static_cast<int>(i));

		const auto label = std::format("Model {}", i);

		if (debugUI_.CollapsingHeader(label.c_str())) {
			Transform &transform = world_->GetTransform(objects_[i]);

			debugUI_.DragFloat3("Position", transform.translation, 0.01f,
			                    -10.0f, 10.0f);

			const std::size_t materialCount =
			    world_->GetMaterialCount(objects_[i]);

			for (std::size_t materialIndex = 0; materialIndex < materialCount;
			     ++materialIndex) {
				debugUI_.PushID(static_cast<int>(materialIndex));

				debugUI_.Separator();
				debugUI_.Text(std::format("Material {}", materialIndex));

				auto &material =
				    world_->GetMaterial(objects_[i], materialIndex);

				DirectX::XMFLOAT3 color{
				    material.baseColor.x,
				    material.baseColor.y,
				    material.baseColor.z,
				};

				// RGBだけを編集し、アルファ値は保持する
				if (debugUI_.ColorEdit3("Base color", color)) {
					material.baseColor.x = color.x;
					material.baseColor.y = color.y;
					material.baseColor.z = color.z;
				}

				debugUI_.PopID();
			}
		}

		debugUI_.PopID();
	}

	debugUI_.EndWindow();
}

#endif

void GameScene::Finalize() {
	// GPUの利用完了後、物体を所有するワールドから解放する
	world_.reset();

	objects_ = {};
	model_ = {};

	autoRotate_ = true;
	initialized_ = false;
}
