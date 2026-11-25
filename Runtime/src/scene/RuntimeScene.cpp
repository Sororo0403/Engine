#include "scene/RuntimeScene.h"
#include "core/EngineContext.h"
#include "core/Logger.h"
#include "gameplay/CharacterInputReader.h"
#include "gameplay/OrbitCameraInputReader.h"
#include "gameplay/SceneValidation.h"
#include "graphics/model/ModelManager.h"
#include "graphics/render/RenderSystem.h"
#include "input/InputSystem.h"
#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"
#include "scene/SceneSerializer.h"
#include "ui/UIInputReader.h"
#include <format>
#include <json.hpp>
#include <utility>

RuntimeScene::RuntimeScene(const EngineContext &context, GameConfig config,
                           SaveMode saveMode)
    : input_(context.input), render_(context.render),
      config_(std::move(config)),
      saveMode_(saveMode),
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
	SceneObjectId invalidObject = 0;
	const auto reportStartupError = [&](std::string_view reason, std::string_view inputAction = {}) {
		auto message = "Could not prepare startup scene: " + PathUtils::ToUtf8(config_.scenePath) +
		               "\n" + std::string(reason);
		if (source.Contains(invalidObject)) {
			message += std::format("\nSource object ID: {}", invalidObject);
		}
		if (!inputAction.empty()) {
			message += "\nInput action: " + nlohmann::json(inputAction).dump();
		}
		Logger::Error(message, config_.scenePath);
	};

	if (!SceneSerializer::Load(source, config_.scenePath, &error)) {
		Logger::Error("Could not load startup scene: " +
		              PathUtils::ToUtf8(config_.scenePath) + "\n" + error);
		return false;
	}

	const auto inputIssues = SceneValidation::InspectInputs(source, config_.actions);
	if (!inputIssues.empty()) {
		invalidObject = inputIssues.front().objectId;
		reportStartupError(inputIssues.front().message, inputIssues.front().inputAction);
		return false;
	}

	if (!renderer_.Initialize()) {
		reportStartupError("Could not initialize the scene renderer.");
		return false;
	}

	savedScriptSettings_ = "{}";
	scriptSettingsPath_.clear();
	observedScriptData_.clear();
	nextScriptSettingsWrite_ = {};
	if (saveMode_ == SaveMode::SESSION_ONLY) {
		Logger::Info("Save data is disabled for this run.");
	} else if (!config_.settingsId.empty()) {
		scriptSettingsPath_ = config_.GetScriptSettingsPath(saveMode_ == SaveMode::EDITOR_TEST);
		if (scriptSettingsPath_.empty()) {
			Logger::Warning("Game script settings directory is unavailable.");
		} else if (config_.LoadScriptSettings(scriptSettingsPath_, savedScriptSettings_, error)) {
			const auto *label = saveMode_ == SaveMode::EDITOR_TEST ? "Editor test save file: "
			                                                     : "Game save file: ";
			Logger::Info(label + PathUtils::ToUtf8(scriptSettingsPath_), scriptSettingsPath_);
		} else {
			// 読めなかったデータを初期値で上書きしない。
			Logger::Warning(error + " Game progress will not be saved during this run.", scriptSettingsPath_);
			scriptSettingsPath_.clear();
		}
	}

	if (!simulation_.Start(source, error, config_.fallbackCamera, &invalidObject,
	                       savedScriptSettings_) ||
	    !renderer_.PrepareSceneModels(error, &invalidObject)) {
		reportStartupError(error);
		return false;
	}

	if (!audioPlayer_.Start(simulation_.GetCameraPose(),
	                        simulation_.IsGamePaused(), error, &invalidObject,
	                        simulation_.GetAudioActions())) {
		reportStartupError(error);
		return false;
	}
	Logger::Info("Startup scene ready: " + PathUtils::ToUtf8(config_.scenePath), config_.scenePath);
	return true;
}

void RuntimeScene::SaveScriptSettings() {
	const auto current = simulation_.GetSharedScriptData();
	if (scriptSettingsPath_.empty() || current == observedScriptData_)
		return;
	const auto now = std::chrono::steady_clock::now();
	if (now < nextScriptSettingsWrite_)
		return;
	auto settings = config_.SelectScriptSettings(current);
	if (settings == savedScriptSettings_) {
		observedScriptData_ = current;
		// 保存前の値に戻った場合は保留していた再試行を取り消す。
		nextScriptSettingsWrite_ = {};
		return;
	}

	std::error_code directoryError;
	std::filesystem::create_directories(scriptSettingsPath_.parent_path(),
	                                    directoryError);
	std::string writeError;
	if (directoryError ||
	    !FileUtils::WriteTextAtomically(scriptSettingsPath_, settings,
	                                    writeError)) {
		Logger::Warning("Could not save game script settings: " +
		                PathUtils::ToUtf8(scriptSettingsPath_) + "\n" +
		                (directoryError ? directoryError.message()
		                                : writeError), scriptSettingsPath_);
		nextScriptSettingsWrite_ = now + std::chrono::seconds(1);
		return;
	}
	savedScriptSettings_ = std::move(settings);
	observedScriptData_ = current;
	if (nextScriptSettingsWrite_ != std::chrono::steady_clock::time_point{}) {
		nextScriptSettingsWrite_ = {};
		Logger::Info("Game script settings saved after retry: " +
		             PathUtils::ToUtf8(scriptSettingsPath_), scriptSettingsPath_);
	}
}

bool RuntimeScene::Update(float seconds) {
	CharacterInput input;
	const auto actions = actionReader_.Read(
	    config_.actions, input_.GetKeyboard(), &input_.GetMouse(),
	    config_.gamepadEnabled ? &input_.GetGamepad() : nullptr,
	    input_.IsActive(), true);
	const auto revision = simulation_.GetSceneRevision();
	SceneObjectId invalidObject = 0;
	const auto reportSceneError = [&](std::string_view reason) {
		const auto *loaded = simulation_.GetLoadedScenePath();
		const auto &path = loaded ? *loaded : config_.scenePath;
		const bool changed = simulation_.GetSceneRevision() != revision && loaded;
		auto message = std::string(reason) +
		               (changed ? "\nDestination scene: " : "\nRunning scene: ") +
		               PathUtils::ToUtf8(path);
		if (simulation_.HasSourceObject(invalidObject)) {
			message += std::format("\nSource object ID: {}", invalidObject);
		}
		Logger::Error(message, path);
	};

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
		        uiInput, actions, &invalidObject,
		        std::span<const GameActionBinding>{config_.actions})) {
			if (const auto failed = simulation_.GetFailedSceneDestination()) {
				auto error = simulation_.GetLastUpdateError();
				if (failed->objectId != 0) {
					error += std::format("\nSource object ID: {}", failed->objectId);
				}
				if (!failed->inputAction.empty()) {
					error += "\nInput action: " + nlohmann::json(failed->inputAction).dump();
				}
				Logger::Error(error, failed->path);
			} else {
				reportSceneError(simulation_.GetLastUpdateError());
			}
			return false;
		}
		SaveScriptSettings();
	}

	if (simulation_.IsQuitRequested()) {
		close_ = true;
		return true;
	}

	if (!input_.IsActive())
		simulation_.CancelUIInput();
	std::string audioError;
	const bool sceneChanged = simulation_.GetSceneRevision() != revision;
	const auto *destination = sceneChanged ? simulation_.GetLoadedScenePath() : nullptr;
	if (sceneChanged && !renderer_.PrepareSceneModels(audioError, &invalidObject)) {
		reportSceneError(audioError);
		return false;
	}
	const bool audioReady =
	    sceneChanged
	        ? audioPlayer_.Start(
	              simulation_.GetCameraPose(),
	              !input_.IsActive() || simulation_.IsGamePaused(), audioError,
	              &invalidObject, simulation_.GetAudioActions())
	        : audioPlayer_.Update(
	              simulation_.GetCameraPose(),
	              !input_.IsActive() || simulation_.IsGamePaused(), audioError,
	              &invalidObject, simulation_.GetAudioActions());
	if (!audioReady) {
		reportSceneError(audioError);
		return false;
	}

	std::vector<AnimatorDrawPose> poses;
	if (!simulation_.GetAnimatorPoses(poses, audioError)) {
		reportSceneError(audioError);
		return false;
	}
	if (!renderer_.SetAnimatorPoses(std::move(poses))) {
		reportSceneError("Could not apply animator poses for rendering.");
		return false;
	}
	std::vector<ParticleDrawBatch> particles;

	if (!simulation_.GetParticles(particles, audioError)) {
		reportSceneError(audioError);
		return false;
	}

	renderer_.SetParticles(std::move(particles));
	const bool renderReady =
	       renderer_.SetUIButtonVisuals(simulation_.GetUI().GetVisuals()) &&
	       renderer_.SetLighting(config_.lighting) &&
	       renderer_.SetCameraPose(simulation_.GetCameraPose()) &&
	       renderer_.SetCameraSettings(simulation_.GetCameraSettings()) &&
	       renderer_.Update();
	if (!renderReady) {
		reportSceneError("Could not prepare the scene for rendering.");
	} else if (destination) {
		Logger::Info("Destination scene ready: " + PathUtils::ToUtf8(*destination), *destination);
	}
	return renderReady;
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
