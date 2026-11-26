#include "gameplay/EditorPlaySession.h"
#include "gameplay/GameConfig.h"
#include "gameplay/SceneScripts.h"
#include "gameplay/SceneValidation.h"
#include "scene/SceneActivity.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

bool EditorPlaySession::Start(const Scene &scene, std::string &error,
                              const CameraPose &fallbackPose,
                              SceneObjectId *invalidObject,
                              SceneObjectId cameraOverride,
                              std::string initialScriptData) {
	if (invalidObject) {
		*invalidObject = 0;
	}

	try {
		// 再生状態を置き換える前に、再実行用の領域を確保する。
		auto snapshot = std::make_unique<Scene>();
		snapshot->CopyFrom(scene);

		if (cameraOverride != 0) {
			const auto *selected = snapshot->FindObject(cameraOverride);

			if (!selected ||
			    !SceneActivity::IsActive(*snapshot, cameraOverride) ||
			    !selected->camera) {
				error = "Choose an existing active Camera for Play.";

				if (invalidObject) {
					*invalidObject = cameraOverride;
				}

				return false;
			}

			// Primaryの重複など、元のシーンの問題を上書き指定で隠さない。
			// 指定Cameraを使うため、未使用の予備姿勢は標準値で検証する。
			const auto issues =
			    SceneValidation::Inspect(*snapshot, CameraPose{});

			if (!issues.empty()) {
				error = issues.front().message;

				if (invalidObject) {
					*invalidObject = issues.front().objectId;
				}

				return false;
			}

			for (const auto &[id, object] : snapshot->GetObjects()) {
				if (object.camera) {
					snapshot->FindObject(id)->camera->primary =
					    id == cameraOverride;
				}
			}
		}

		if (!simulation_.Start(*snapshot, error, fallbackPose, invalidObject,
		                       initialScriptData)) {
			return false;
		}

		initialScene_ = std::move(snapshot);
		initialSceneRevision_ = simulation_.GetSceneRevision();
		initialFallback_ = fallbackPose;
		initialScriptData_ = std::move(initialScriptData);
		clock_.Reset();
		elapsedSeconds_ = 0.0;
		simulationFrameCount_ = 0;
		firstFrame_ = true;
		triggerRecordCount_ = 0;
		RememberTriggerEvents();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool EditorPlaySession::Restart(std::string &error, bool currentScene,
                                std::filesystem::path *failedScript,
                                SceneObjectId *sourceObject) {
	if (failedScript) {
		failedScript->clear();
	}
	if (sourceObject) {
		*sourceObject = 0;
	}
	if (!IsPlaying() || !initialScene_) {
		error = "Start Play before restarting.";
		return false;
	}

	const bool isInitial = !currentScene || IsInitialScene();
	SceneObjectId invalidObject = 0;
	const bool restarted = currentScene
	                           ? simulation_.RestartCurrentScene(error, failedScript, sourceObject)
	                           : simulation_.Start(*initialScene_, error, initialFallback_,
	                                               &invalidObject, initialScriptData_);
	if (!restarted) {
		if (!currentScene && failedScript) {
			*failedScript = SceneScripts::FindFailedScript(*initialScene_, invalidObject, error);
		}
		if (!currentScene && sourceObject && initialScene_->Contains(invalidObject)) {
			*sourceObject = invalidObject;
		}
		return false;
	}

	if (isInitial) {
		initialSceneRevision_ = simulation_.GetSceneRevision();
	}
	const bool paused = clock_.IsPaused();
	clock_.Reset();

	if (paused) {
		clock_.TogglePause();
		simulation_.CancelUIInput();
	}

	elapsedSeconds_ = 0.0;
	simulationFrameCount_ = 0;
	firstFrame_ = true;
	triggerRecordCount_ = 0;
	RememberTriggerEvents();
	return true;
}

void EditorPlaySession::Stop() {
	simulation_.Stop();
	initialScene_.reset();
	initialSceneRevision_ = 0;
	initialFallback_ = {};
	initialScriptData_.clear();
	clock_.Reset();
	elapsedSeconds_ = 0.0;
	simulationFrameCount_ = 0;
	firstFrame_ = false;
	triggerRecordCount_ = 0;
}

void EditorPlaySession::TogglePause() {
	if (IsPlaying()) {
		clock_.TogglePause();
	}
}

void EditorPlaySession::RequestStep() {
	if (IsPlaying()) {
		clock_.RequestStep();
	}
}

double EditorPlaySession::GetElapsedSeconds() const {
	return elapsedSeconds_;
}

std::uint64_t EditorPlaySession::GetSimulationFrameCount() const {
	return simulationFrameCount_;
}

bool EditorPlaySession::Update(float seconds, const CharacterInput &input,
                               const OrbitCameraInput &cameraInput,
                               const UIInput &uiInput,
                               std::span<const GameActionState> actions,
                               SceneObjectId *invalidObject,
                               std::optional<std::span<const GameActionBinding>> destinationBindings,
                               float frameTimeLimit) {
	if (invalidObject) {
		*invalidObject = 0;
	}

	if (!IsPlaying() || !std::isfinite(seconds) || seconds < 0) {
		return false;
	}

	if (firstFrame_) {
		firstFrame_ = false;
		// 通常再生では初期化にかかった時間を除外する。一時停止中のStepは固定時間で進める。
		if (!clock_.IsPaused()) {
			return true;
		}
	}

	const auto time = clock_.Advance(seconds);

	if (!time) {
		simulation_.CancelUIInput();
		return true;
	}

	const auto revision = simulation_.GetSceneRevision();
	const bool gamePaused = simulation_.IsGamePaused();

	const float elapsed = GameConfig::LimitFrameTime(*time, frameTimeLimit);
	if (!simulation_.Update(elapsed, input, cameraInput, uiInput, actions,
	                        invalidObject, destinationBindings)) {
		return false;
	}

	if (simulation_.GetSceneRevision() != revision)
		triggerRecordCount_ = 0;
	if (elapsed > 0.0f && !gamePaused) {
		elapsedSeconds_ += static_cast<double>(elapsed);
		++simulationFrameCount_;
		RememberTriggerEvents();
	}

	return true;
}

bool EditorPlaySession::IsPlaying() const {
	return simulation_.IsPlaying();
}

bool EditorPlaySession::IsPaused() const {
	return IsPlaying() && clock_.IsPaused();
}

std::optional<CharacterRuntimeState>
EditorPlaySession::GetCharacterState() const {
	return simulation_.GetCharacterState();
}

const Scene &EditorPlaySession::GetScene() const {
	return simulation_.GetScene();
}

SceneObjectId EditorPlaySession::GetCameraObjectId() const {
	return simulation_.GetCameraObjectId();
}

const CameraPose &EditorPlaySession::GetCameraPose() const {
	return simulation_.GetCameraPose();
}

const CameraSettings &EditorPlaySession::GetCameraSettings() const {
	return simulation_.GetCameraSettings();
}

std::span<const TriggerEvent> EditorPlaySession::GetTriggerEvents() const {
	return simulation_.GetTriggerEvents();
}

std::span<const TriggerContact> EditorPlaySession::GetTriggerContacts() const {
	return simulation_.GetTriggerContacts();
}

std::span<const EditorPlaySession::TriggerRecord>
EditorPlaySession::GetTriggerHistory() const {
	return {triggerRecords_.data(), triggerRecordCount_};
}

void EditorPlaySession::RememberTriggerEvents() {
	for (const auto &event : simulation_.GetTriggerEvents()) {
		if (event.type == TriggerEvent::Type::STAY) {
			continue;
		}

		if (triggerRecordCount_ == triggerRecords_.size()) {
			std::move(triggerRecords_.begin() + 1, triggerRecords_.end(),
			          triggerRecords_.begin());
			--triggerRecordCount_;
		}

		triggerRecords_[triggerRecordCount_++] = {event, simulationFrameCount_,
		                                          elapsedSeconds_};
	}
}
