#include "RuntimeScene.h"
#include "core/EngineContext.h"
#include "core/Logger.h"
#include "gameplay/CharacterInputReader.h"
#include "input/InputSystem.h"
#include "scene/SceneSerializer.h"
#include <utility>

RuntimeScene::RuntimeScene(const EngineContext &context, GameConfig config)
    : input_(context.input), config_(std::move(config)),
      renderer_(simulation_.GetScene(), context.models, context.render) {}
bool RuntimeScene::Initialize() {
	Scene source;
	std::string error;
	if (!SceneSerializer::Load(source, config_.scenePath)) {
		return false;
	}
	if (!simulation_.Start(source, error, config_.fallbackCamera)) {
		Logger::Error(error);
		return false;
	}
	return renderer_.Initialize();
}
bool RuntimeScene::Update(float seconds) {
	CharacterInput input;
	if (input_.IsActive()) {
		const auto &keys = input_.GetKeyboard();
		if (keys.IsPressed(Key::ESCAPE)) {
			close_ = true;
			return true;
		}
		input = CharacterInputReader::Read(keys);
		if (!simulation_.Update(seconds, input)) {
			return false;
		}
	}
	return renderer_.SetCameraPose(simulation_.GetCameraPose()) &&
	       renderer_.SetCameraSettings(simulation_.GetCameraSettings()) &&
	       renderer_.Update();
}
void RuntimeScene::Draw() const { renderer_.Draw(); }
void RuntimeScene::Finalize() {
	renderer_.Finalize();
	simulation_.Stop();
}
bool RuntimeScene::IsCloseApproved() const { return close_; }
