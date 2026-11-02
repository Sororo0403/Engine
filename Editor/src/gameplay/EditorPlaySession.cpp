#include "gameplay/EditorPlaySession.h"

#include <cmath>
#include <exception>
#include <utility>

bool EditorPlaySession::Start(const Scene &scene, std::string &error,
                              const CameraPose &fallbackPose,
                              SceneObjectId *invalidObject) {
	if (invalidObject) {
		*invalidObject = 0;
	}

	try {
		// 再生状態を置き換える前に、再実行用の領域を確保する。
		auto snapshot = std::make_unique<Scene>();
		snapshot->ReplaceObjects(scene.GetObjects());

		if (!simulation_.Start(*snapshot, error, fallbackPose, invalidObject)) {
			return false;
		}

		initialScene_ = std::move(snapshot);
		initialFallback_ = fallbackPose;
		clock_.Reset();
		firstFrame_ = true;
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool EditorPlaySession::Restart(std::string &error) {
	if (!IsPlaying() || !initialScene_) {
		error = "Start Play before restarting.";
		return false;
	}

	if (!simulation_.Start(*initialScene_, error, initialFallback_)) {
		return false;
	}

	const bool paused = clock_.IsPaused();
	clock_.Reset();

	if (paused) {
		clock_.TogglePause();
	}

	firstFrame_ = true;
	return true;
}

void EditorPlaySession::Stop() {
	simulation_.Stop();
	initialScene_.reset();
	initialFallback_ = {};
	clock_.Reset();
	firstFrame_ = false;
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

bool EditorPlaySession::Update(float seconds, const CharacterInput &input) {
	if (!IsPlaying() || !std::isfinite(seconds) || seconds < 0) {
		return false;
	}

	if (firstFrame_) {
		firstFrame_ = false;
		return true;
	}

	const auto time = clock_.Advance(seconds);
	return !time || simulation_.Update(*time, input);
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

const CameraPose &EditorPlaySession::GetCameraPose() const {
	return simulation_.GetCameraPose();
}

const CameraSettings &EditorPlaySession::GetCameraSettings() const {
	return simulation_.GetCameraSettings();
}
