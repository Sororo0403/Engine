#include "scene/GameScene.h"
#include "core/Logger.h"

#include "graphics/Transform.h"
#include "graphics/debug/DebugUI.h"
#include "graphics/model/ModelData.h"
#include "graphics/model/ModelLoader.h"
#include "graphics/render/D3D12Graphics.h"
#include "graphics/render/MeshRenderer.h"

#include "input/InputSystem.h"
#include "input/Key.h"
#include "platform/windows/WinApp.h"

#include <imgui.h>

#include <cmath>
#include <exception>
#include <format>

GameScene::GameScene(D3D12Graphics &graphics, MeshRenderer &renderer,
                     const InputSystem &input, const WinApp &app,
                     const DebugUI &debugUI)
    : graphics_(graphics), renderer_(renderer), input_(input), app_(app),
      debugUI_(debugUI) {}

bool GameScene::Initialize() {
	if (initialized_) {
		Logger::Error("GameScene::Initialize: 初期化済みです。");
		return false;
	}

	if (!graphics_.GetDevice() || !graphics_.GetCommandQueue()) {
		Logger::Error("GameScene::Initialize: 描画基盤が未初期化です。");
		return false;
	}

	try {
		if (!CreateModel() || !CreateInstances() ||
		    !light_.Initialize(graphics_.GetDevice())) {
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
	ModelData data;

	if (!ModelLoader::Load(L"resources/models/box.glb", data)) {
		return false;
	}

	return model_.Initialize(graphics_.GetDevice(), graphics_.GetCommandQueue(),
	                         data);
}

bool GameScene::CreateInstances() {
	for (std::size_t i = 0; i < instances_.size(); ++i) {
		if (!instances_[i].Initialize(graphics_.GetDevice(), model_)) {
			return false;
		}

		Transform transform{};
		transform.scale = {0.45f, 0.45f, 0.45f};
		transform.rotation = {0.3f, 0.4f, 0.0f};
		transform.translation.x = (i == 0) ? -0.6f : 0.6f;

		instances_[i].SetTransform(transform);
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

	// 編集してからGPU用データを更新する
	DrawDebugControls();

	if (!UpdateCamera(deltaTime)) {
		return false;
	}

	if (!light_.Update(lightData_)) {
		return false;
	}

	for (std::size_t i = 0; i < instances_.size(); ++i) {
		Transform transform = instances_[i].GetTransform();

		if (autoRotate_) {
			const float rotationSpeed = (i == 0) ? 1.0f : -0.7f;

			transform.rotation.y += rotationSpeed * deltaTime;
		}

		instances_[i].SetTransform(transform);

		if (!instances_[i].Update(camera_)) {
			return false;
		}
	}

	return true;
}

bool GameScene::UpdateCamera(float deltaTime) {
	const Keyboard &keyboard = input_.GetKeyboard();

	float moveX = 0.0f;
	float moveZ = 0.0f;

	if (!debugUI_.WantsKeyboard()) {
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
	}

	const float length = std::sqrt(moveX * moveX + moveZ * moveZ);

	if (length > 0.0f) {
		constexpr float moveSpeed = 2.0f;
		const float distance = moveSpeed * deltaTime;

		camera_.Translate(
		    {moveX / length * distance, 0.0f, moveZ / length * distance});
	}

	if (!camera_.Update(app_.GetWidth(), app_.GetHeight())) {
		Logger::Error("GameScene: カメラの更新に失敗しました。");
		return false;
	}

	return true;
}

void GameScene::Draw() const {
	if (!initialized_) {
		Logger::Error("GameScene::Draw: 未初期化です。");
		return;
	}

	auto *commandList = graphics_.GetCommandList();

	if (!commandList) {
		Logger::Error("GameScene::Draw: コマンドリストがありません。");
		return;
	}

	for (const auto &instance : instances_) {
		instance.Draw(commandList, renderer_, light_);
	}
}

void GameScene::DrawDebugControls() {
	if (!ImGui::Begin("Game Scene")) {
		ImGui::End();
		return;
	}

	ImGui::Checkbox("Auto rotate", &autoRotate_);

	if (ImGui::CollapsingHeader("Directional Light",
	                            ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Intensity", &lightData_.intensity, 0.0f, 5.0f,
		                   "%.2f", ImGuiSliderFlags_AlwaysClamp);

		ImGui::SliderFloat("Ambient", &lightData_.ambient, 0.0f, 1.0f, "%.2f",
		                   ImGuiSliderFlags_AlwaysClamp);

		float color[] = {lightData_.color.x, lightData_.color.y,
		                 lightData_.color.z};

		if (ImGui::ColorEdit3("Light color", color)) {
			lightData_.color = {color[0], color[1], color[2]};
		}
	}

	for (std::size_t i = 0; i < instances_.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));

		const auto label = std::format("Model {}", i);

		if (ImGui::CollapsingHeader(label.c_str(),
		                            ImGuiTreeNodeFlags_DefaultOpen)) {
			ModelInstance &instance = instances_[i];

			Transform transform = instance.GetTransform();

			float position[] = {transform.translation.x,
			                    transform.translation.y,
			                    transform.translation.z};

			if (ImGui::DragFloat3("Position", position, 0.01f, -10.0f, 10.0f,
			                      "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
				transform.translation = {position[0], position[1], position[2]};

				instance.SetTransform(transform);
			}

			for (std::size_t materialIndex = 0;
			     materialIndex < instance.GetMaterialCount(); ++materialIndex) {
				ImGui::PushID(static_cast<int>(materialIndex));

				ImGui::Separator();
				ImGui::Text("Material %zu", materialIndex);

				auto &material = instance.GetMaterialData(materialIndex);

				float color[] = {material.baseColor.x, material.baseColor.y,
				                 material.baseColor.z};

				if (ImGui::ColorEdit3("Base color", color)) {
					material.baseColor.x = color[0];
					material.baseColor.y = color[1];
					material.baseColor.z = color[2];
				}

				ImGui::PopID();
			}
		}

		ImGui::PopID();
	}

	ImGui::End();
}

void GameScene::Finalize() {
	// 共有モデルを参照している物体を先に解放する
	for (auto &instance : instances_) {
		instance.Finalize();
	}

	light_.Finalize();
	model_.Finalize();

	camera_ = Camera{};
	lightData_ = DirectionalLightData{};
	autoRotate_ = true;
	initialized_ = false;
}
