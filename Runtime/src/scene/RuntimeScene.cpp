#include "scene/RuntimeScene.h"
#include "core/EngineContext.h"
#include "core/Logger.h"
#include "gameplay/CharacterInputReader.h"
#include "gameplay/OrbitCameraInputReader.h"
#include "input/InputSystem.h"
#include "scene/SceneSerializer.h"
#include <utility>

RuntimeScene::RuntimeScene(const EngineContext &context, GameConfig config)
    : input_(context.input), config_(std::move(config)),
      renderer_(simulation_.GetScene(), context.models, context.render),
      audioPlayer_(simulation_.GetScene(), context.audioClips, context.audio) {
}

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

	if (!renderer_.Initialize())
		return false;
	if (!audioPlayer_.Start(simulation_.GetCameraPose(), false, error, nullptr,
	                        simulation_.GetAudioActions())) {
		Logger::Error(error);
		return false;
	}
	return true;
}

bool RuntimeScene::Update(float seconds) {
	CharacterInput input;

	if (input_.IsActive()) {
		const auto &keys = input_.GetKeyboard();

		if (keys.IsPressed(Key::ESCAPE)) {
			close_ = true;
			return true;
		}

		input = CharacterInputReader::Read(
		    keys, config_.input,
		    config_.cameraRelativeMovement ? &simulation_.GetCameraPose()
		                                   : nullptr,
		    config_.gamepadEnabled ? &input_.GetGamepad() : nullptr);

		if (!simulation_.Update(
		        seconds, input,
		        OrbitCameraInputReader::Read(
		            &input_.GetMouse(),
		            config_.gamepadEnabled ? &input_.GetGamepad() : nullptr))) {
			Logger::Error(simulation_.GetLastUpdateError());
			return false;
		}
	}

	std::string audioError;
	if (!audioPlayer_.Update(simulation_.GetCameraPose(), !input_.IsActive(),
	                         audioError, nullptr,
	                         simulation_.GetAudioActions())) {
		Logger::Error(audioError);
		return false;
	}

	return renderer_.SetLighting(config_.lighting) &&
	       renderer_.SetCameraPose(simulation_.GetCameraPose()) &&
	       renderer_.SetCameraSettings(simulation_.GetCameraSettings()) &&
	       renderer_.Update();
}

void RuntimeScene::Draw() const {
	renderer_.Draw();
}

void RuntimeScene::Finalize() {
	audioPlayer_.Stop();
	renderer_.Finalize();
	simulation_.Stop();
}

bool RuntimeScene::IsCloseApproved() const {
	return close_;
}
