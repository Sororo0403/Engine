#include "TutorialScene.h"
#include "core/Logger.h"
#include <imgui.h>

namespace {

constexpr ImVec4 kAccent{0.44f, 0.82f, 0.73f, 1.0f};

} // namespace

bool TutorialScene::Initialize() {
	Logger::Info("EngineTutorial: Initialize");
	return true;
}

bool TutorialScene::Update(float seconds) {
	if (updates_ == 0) {
		Logger::Info("EngineTutorial: Update");
	}
	++updates_;
	elapsed_ += seconds;

	const auto *viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
	if (ImGui::Begin("EngineTutorial", nullptr, ImGuiWindowFlags_NoDecoration |
	                                            ImGuiWindowFlags_NoMove |
	                                            ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::TextColored(kAccent, "EngineTutorial");
		ImGui::Separator();
		DrawStartupLesson();
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
	return true;
}

void TutorialScene::Draw() const {
	if (draws_ == 0) {
		Logger::Info("EngineTutorial: Draw");
	}
	++draws_;
}

void TutorialScene::Finalize() {
	Logger::Info("EngineTutorial: Finalize");
}

void TutorialScene::DrawStartupLesson() const {
	ImGui::SeparatorText("第1章  ウィンドウとメインループ");
	ImGui::TextWrapped("Visual Studioで src/main.cpp を開き、windowTitle を好きな名前に変えてください。");
	ImGui::TextWrapped("windowWidth と windowHeight を変更してビルドし、開いたウィンドウの大きさを確認します。");
	ImGui::TextUnformatted("main.cpp  →  Engine::Run  →  TutorialScene");
	ImGui::Spacing();
	ImGui::SeparatorText("実行結果");
	ImGui::TextUnformatted("Initialize   シーンを準備しました");
	ImGui::Text("Update      %llu 回", static_cast<unsigned long long>(updates_));
	ImGui::Text("Draw        %llu 回", static_cast<unsigned long long>(draws_));
	ImGui::Text("経過時間    %.2f 秒", static_cast<double>(elapsed_));
	ImGui::TextWrapped("閉じたあと、出力先の logs/Engine.log で Initialize、Update、Draw、Finalize の順番を確認できます。");
}
