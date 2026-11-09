#include "scene/RuntimeScene.h"
#include "core/EngineContext.h"
#include "core/Logger.h"
#include "gameplay/CharacterInputReader.h"
#include "gameplay/OrbitCameraInputReader.h"
#include "graphics/model/ModelManager.h"
#include "graphics/render/RenderSystem.h"
#include "input/InputSystem.h"
#include "scene/SceneSerializer.h"
#include "ui/UIInputReader.h"
#include <utility>

RuntimeScene::RuntimeScene(const EngineContext &context, GameConfig config)
    : input_(context.input), render_(context.render),
      config_(std::move(config)),
      renderer_(simulation_.GetScene(), context.models, context.render),
      audioPlayer_(simulation_.GetScene(), context.audioClips, context.audio) {
	simulation_.SetLegacyLighting(config_.lighting);
	simulation_.SetAnimatorModelLoading(
	    [&models = context.models](const std::filesystem::path &path,
	                               std::string &error) {
		    auto data = models.GetAnimationData(models.Load(path));
		    if (!data)
			    error = "The model has no usable animation data.";
		    else
			    error.clear();
		    return data;
	    });
	simulation_.SetPrefabResourcePreparation(
	    [this, &clips = context.audioClips](auto prefabs, std::string &error) {
		    return renderer_.PreparePrefabResources(prefabs, clips, error);
	    });
}

bool RuntimeScene::Initialize() {
	actionReader_.Reset();
	Scene source;
	std::string error;

	if (!SceneSerializer::Load(source, config_.scenePath)) {
		return false;
	}

	if (!renderer_.Initialize())
		return false;
	if (!simulation_.Start(source, error, config_.fallbackCamera)) {
		Logger::Error(error);
		return false;
	}

	if (!audioPlayer_.Start(simulation_.GetCameraPose(),
	                        simulation_.IsGamePaused(), error, nullptr,
	                        simulation_.GetAudioActions())) {
		Logger::Error(error);
		return false;
	}
	return true;
}

bool RuntimeScene::Update(float seconds) {
	CharacterInput input;
	const auto actions = actionReader_.Read(
	    config_.actions, input_.GetKeyboard(), &input_.GetMouse(),
	    config_.gamepadEnabled ? &input_.GetGamepad() : nullptr,
	    input_.IsActive());
	const auto revision = simulation_.GetSceneRevision();

	if (input_.IsActive()) {
		const auto &keys = input_.GetKeyboard();

		if (keys.IsPressed(Key::ESCAPE) &&
		    !GameActionBinding::UsesKey(config_.actions, Key::ESCAPE)) {
			close_ = true;
			return true;
		}

		input = CharacterInputReader::Read(
		    keys, config_.input,
		    config_.cameraRelativeMovement ? &simulation_.GetCameraPose()
		                                   : nullptr,
		    config_.gamepadEnabled ? &input_.GetGamepad() : nullptr);

		const auto size = render_.GetWindowSize();
		const auto uiInput = UIInputReader::Read(
		    keys, &input_.GetMouse(),
		    config_.gamepadEnabled ? &input_.GetGamepad() : nullptr, size[0],
		    size[1]);
		if (!simulation_.Update(
		        seconds, input,
		        OrbitCameraInputReader::Read(
		            &input_.GetMouse(),
		            config_.gamepadEnabled ? &input_.GetGamepad() : nullptr),
		        uiInput, actions)) {
			Logger::Error(simulation_.GetLastUpdateError());
			return false;
		}
	}

	if (simulation_.IsQuitRequested()) {
		close_ = true;
		return true;
	}

	if (!input_.IsActive())
		simulation_.CancelUIInput();
	std::string audioError;
	const bool audioReady =
	    simulation_.GetSceneRevision() != revision
	        ? audioPlayer_.Start(
	              simulation_.GetCameraPose(),
	              !input_.IsActive() || simulation_.IsGamePaused(), audioError,
	              nullptr, simulation_.GetAudioActions())
	        : audioPlayer_.Update(
	              simulation_.GetCameraPose(),
	              !input_.IsActive() || simulation_.IsGamePaused(), audioError,
	              nullptr, simulation_.GetAudioActions());
	if (!audioReady) {
		Logger::Error(audioError);
		return false;
	}

	std::vector<AnimatorDrawPose> poses;
	if (!simulation_.GetAnimatorPoses(poses, audioError) ||
	    !renderer_.SetAnimatorPoses(std::move(poses)))
		return false;
	std::vector<ParticleDrawBatch> particles;

	if (!simulation_.GetParticles(particles, audioError)) {
		Logger::Error(audioError);
		return false;
	}

	renderer_.SetParticles(std::move(particles));
	return renderer_.SetUIButtonVisuals(simulation_.GetUI().GetVisuals()) &&
	       renderer_.SetLighting(config_.lighting) &&
	       renderer_.SetCameraPose(simulation_.GetCameraPose()) &&
	       renderer_.SetCameraSettings(simulation_.GetCameraSettings()) &&
	       renderer_.Update();
}

void RuntimeScene::Draw() const {
	if (!close_)
		renderer_.Draw();
}

void RuntimeScene::Finalize() {
	actionReader_.Reset();
	audioPlayer_.Stop();
	renderer_.Finalize();
	simulation_.Stop();
}

bool RuntimeScene::IsCloseApproved() const {
	return close_;
}
