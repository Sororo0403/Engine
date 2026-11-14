#include "scene/RuntimeScene.h"
#include "core/EngineContext.h"
#include "core/Logger.h"
#include "gameplay/CharacterInputReader.h"
#include "gameplay/OrbitCameraInputReader.h"
#include "graphics/model/ModelManager.h"
#include "graphics/render/RenderSystem.h"
#include "input/InputSystem.h"
#include "platform/windows/FileUtils.h"
#include "scene/SceneSerializer.h"
#include "ui/UIInputReader.h"
#include <cstdlib>
#include <fstream>
#include <json.hpp>
#include <memory>
#include <utility>

namespace {
constexpr std::size_t kMaxScriptSettingsBytes = 32768;

/// <summary>
/// ゲームごとのユーザー設定ファイルの場所を取得する
/// </summary>
std::filesystem::path GetScriptSettingsPath(const std::string &settingsId) {
	wchar_t *localAppData = nullptr;
	const auto result = _wdupenv_s(&localAppData, nullptr, L"LOCALAPPDATA");
	const std::unique_ptr<wchar_t, decltype(&std::free)> directory(
	    localAppData, &std::free);
	if (result != 0 || !directory)
		return {};
	const std::filesystem::path root(directory.get());
	if (!root.is_absolute())
		return {};
	return root / L"Engine" / L"GameSettings" /
	       ("game-" + settingsId + ".json");
}

/// <summary>
/// 設定に登録されたキーから、保存可能な単一の値だけを取り出す
/// </summary>
std::string SelectScriptSettings(std::string_view text,
                                 const std::vector<std::string> &keys) {
	const auto data = nlohmann::json::parse(text, nullptr, false);
	auto settings = nlohmann::json::object();
	if (data.is_object()) {
		for (const auto &key : keys) {
			const auto value = data.find(key);
			if (value == data.end())
				continue;
			if (value->is_boolean() || value->is_number() ||
			    (value->is_string() &&
			     value->get_ref<const std::string &>().size() <= 256)) {
				settings[key] = *value;
			}
		}
	}
	return settings.dump();
}

/// <summary>
/// ユーザー設定を上限付きで読み込み、起動時の共有データを作る
/// </summary>
std::string LoadScriptSettings(const std::filesystem::path &path,
                               const std::vector<std::string> &keys) {
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return "{}";
	std::string text(kMaxScriptSettingsBytes + 1, '\0');
	file.read(text.data(), static_cast<std::streamsize>(text.size()));
	if (!file.eof() || file.bad()) {
		Logger::Warning("Could not read game script settings.");
		return "{}";
	}
	text.resize(static_cast<std::size_t>(file.gcount()));
	return SelectScriptSettings(text, keys);
}
} // namespace

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

	if (!SceneSerializer::Load(source, config_.scenePath, &error)) {
		Logger::Error("Could not load startup scene: " +
		              PathUtils::ToUtf8(config_.scenePath) + "\n" + error);
		return false;
	}

	if (!renderer_.Initialize())
		return false;

	savedScriptSettings_ = "{}";
	observedScriptData_.clear();
	if (!config_.settingsId.empty()) {
		scriptSettingsPath_ = GetScriptSettingsPath(config_.settingsId);
		if (scriptSettingsPath_.empty())
			Logger::Warning("Game script settings directory is unavailable.");
		else
			savedScriptSettings_ = LoadScriptSettings(
			    scriptSettingsPath_, config_.persistentScriptKeys);
	}

	if (!simulation_.Start(source, error, config_.fallbackCamera, nullptr,
	                       savedScriptSettings_) ||
	    !renderer_.PrepareSceneModels(error)) {
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

void RuntimeScene::SaveScriptSettings() {
	const auto current = simulation_.GetSharedScriptData();
	if (scriptSettingsPath_.empty() || current == observedScriptData_)
		return;
	observedScriptData_ = current;
	auto settings =
	    SelectScriptSettings(current, config_.persistentScriptKeys);
	if (settings == savedScriptSettings_)
		return;

	std::error_code directoryError;
	std::filesystem::create_directories(scriptSettingsPath_.parent_path(),
	                                    directoryError);
	std::string writeError;
	if (directoryError ||
	    !FileUtils::WriteTextAtomically(scriptSettingsPath_, settings,
	                                    writeError)) {
		Logger::Warning("Could not save game script settings: " +
		                (directoryError ? directoryError.message()
		                                : writeError));
		return;
	}
	savedScriptSettings_ = std::move(settings);
}

bool RuntimeScene::Update(float seconds) {
	CharacterInput input;
	const auto actions = actionReader_.Read(
	    config_.actions, input_.GetKeyboard(), &input_.GetMouse(),
	    config_.gamepadEnabled ? &input_.GetGamepad() : nullptr,
	    input_.IsActive(), true, &input_.GetWiiRemote());
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
	if (sceneChanged && !renderer_.PrepareSceneModels(audioError)) {
		Logger::Error(audioError);
		return false;
	}
	const bool audioReady =
	    sceneChanged
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
