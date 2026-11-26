#include "scene/SceneManager.h"
#include "core/Logger.h"

#include <cmath>
#include <exception>
#include <format>
#include <utility>

bool SceneManager::RequestChange(std::unique_ptr<IScene> nextScene) {
	if (!nextScene) {
		Logger::Error("SceneManager::RequestChange: シーンが無効です。");
		return false;
	}

	if (pendingScene_) {
		Logger::Error(
		    "SceneManager::RequestChange: 切り替えはすでに予約されています。");
		return false;
	}

	pendingScene_ = std::move(nextScene);

	Logger::Info("シーンの切り替えを予約しました。");
	return true;
}

bool SceneManager::ApplyPendingChange() {
	if (!pendingScene_) {
		return true;
	}

	// 初期化処理中に予約用の変数が変わっても、
	// 今回初期化するシーンは保持できるようにする
	auto nextScene = std::move(pendingScene_);

	bool initialized = false;

	try {
		initialized = nextScene->Initialize();
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "次のシーンの初期化中に例外が発生しました: {}", error.what()));
	}

	if (!initialized) {
		// 初期化途中まで作成されたリソースも解放する
		nextScene->Finalize();

		Logger::Error(
		    "次のシーンの初期化に失敗しました。現在のシーンを保持します。");
		return false;
	}

	// 次のシーンの初期化に成功してから、現在のシーンを終了する
	if (currentScene_) {
		currentScene_->Finalize();
	}

	currentScene_ = std::move(nextScene);

	Logger::Info("シーンを切り替えました。");
	return true;
}

bool SceneManager::Update(float deltaTime) {
	if (!currentScene_) {
		Logger::Error("SceneManager::Update: シーンがありません。");
		return false;
	}

	if (!std::isfinite(deltaTime) || deltaTime < 0.0f) {
		Logger::Error("SceneManager::Update: 経過時間が無効です。");
		return false;
	}

	return currentScene_->Update(deltaTime);
}

void SceneManager::Draw() const {
	if (!currentScene_) {
		Logger::Error("SceneManager::Draw: シーンがありません。");
		return;
	}

	currentScene_->Draw();
}

void SceneManager::Finalize() {
	// 予約中のシーンは未初期化なので、そのまま破棄する
	pendingScene_.reset();

	if (currentScene_) {
		currentScene_->Finalize();
		currentScene_.reset();
	}
}

bool SceneManager::RequestClose() {
	return !currentScene_ || currentScene_->RequestClose();
}

bool SceneManager::IsCloseApproved() const {
	return currentScene_ && currentScene_->IsCloseApproved();
}

std::string SceneManager::GetWindowTitle() const {
	return currentScene_ ? currentScene_->GetWindowTitle() : std::string{};
}
