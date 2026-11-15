#include "TutorialScene.h"
#include "BoxLesson.h"
#include "core/EngineContext.h"
#include "core/Logger.h"
#include "graphics/render/RenderSystem.h"
#include "graphics/render/RenderTexture.h"
#include "graphics/ui/ImGuiSystem.h"
#include <algorithm>
#include <imgui.h>

namespace {

constexpr ImVec4 kAccent{0.44f, 0.82f, 0.73f, 1.0f};

} // namespace

TutorialScene::TutorialScene(const EngineContext &context)
    : ui_(context.ui), render_(context.render),
      renderer_(scene_, context.models, context.render) {
}

TutorialScene::~TutorialScene() = default;

bool TutorialScene::Initialize() {
	box_ = CreateLessonBox(scene_);
	initialBoxTransform_ = scene_.FindObject(box_)->transform;
	LightingSettings lighting;
	lighting.ambient = 0.36f;
	if (!renderer_.Initialize() || !renderer_.SetLighting(lighting) ||
	    !renderer_.SetCameraPose({{4.4f, 3.4f, -7.0f}, {0, 0.4f, 0}})) {
		return false;
	}
	Logger::Info("EngineTutorial: Initialize");
	return true;
}

bool TutorialScene::Update(float seconds) {
	if (updates_ == 0) {
		Logger::Info("EngineTutorial: Update");
	}
	++updates_;
	elapsed_ += seconds;
	previewVisible_ = false;
	bool ready = true;

	const auto *viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
	if (ImGui::Begin("EngineTutorial", nullptr, ImGuiWindowFlags_NoDecoration |
	                                            ImGuiWindowFlags_NoMove |
	                                            ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::TextColored(kAccent, "EngineTutorial");
		if (ImGui::Button("1  起動")) {
			chapter_ = Chapter::STARTUP;
		}
		ImGui::SameLine();
		if (ImGui::Button("2  箱の表示")) {
			chapter_ = Chapter::BOX;
		}
		ImGui::Separator();
		ImGui::PushID(static_cast<int>(chapter_));
		if (chapter_ == Chapter::STARTUP) {
			DrawStartupLesson();
		} else {
			const float previewWidth = ImGui::GetContentRegionAvail().x * 0.59f;
			if (ImGui::BeginChild("Preview", {previewWidth, 0}, ImGuiChildFlags_Borders)) {
				ready = DrawPreview();
			}
			ImGui::EndChild();
			ImGui::SameLine();
			if (ImGui::BeginChild("Lesson", {0, 0}, ImGuiChildFlags_Borders)) {
				DrawBoxLesson();
			}
			ImGui::EndChild();
		}
		ImGui::PopID();
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
	return ready && (!previewVisible_ || renderer_.Update(preview_->GetWidth(), preview_->GetHeight()));
}

void TutorialScene::Draw() const {
	if (draws_ == 0) {
		Logger::Info("EngineTutorial: Draw");
	}
	++draws_;
	if (previewVisible_) {
		renderer_.Draw(preview_.get());
	}
}

void TutorialScene::Finalize() {
	preview_.reset();
	renderer_.Finalize();
	Logger::Info("EngineTutorial: Finalize");
}

bool TutorialScene::DrawPreview() {
	ImGui::TextUnformatted("3Dプレビュー");
	const auto size = ImGui::GetContentRegionAvail();
	if (size.x < 1 || size.y < 1) {
		return true;
	}

	const int width = static_cast<int>(std::min(size.x, 4096.f));
	const int height = static_cast<int>(std::min(size.y, 4096.f));
	if (!preview_) {
		preview_ = render_.CreateRenderTexture(width, height);
	}
	if (!preview_ || !preview_->Resize(width, height)) {
		return false;
	}
	ui_.Image(*preview_, static_cast<float>(width), static_cast<float>(height));
	previewVisible_ = true;
	return true;
}

void TutorialScene::DrawBoxLesson() {
	ImGui::SeparatorText("第2章  箱とTransform");
	ImGui::TextWrapped("src/BoxLesson.cpp の CreateLessonBox() を開き、箱の translation.x を 1.5 に変えてビルドしてください。");
	ImGui::TextWrapped("次は scale.y を 2.0 に変更し、箱と黄色い子オブジェクトの位置を見比べます。");
	ImGui::TextWrapped("Scene::CreateObject() が SceneObjectId を発行し、SceneRenderer::Update() が物体の変形とモデルを描画に反映します。");
	ImGui::SeparatorText("その場で試す");
	ui_.SliderFloat("親のY回転", previewYaw_, -180.f, 180.f, "%.0f 度");
	if (ImGui::Button("回転を戻す")) {
		previewYaw_ = 0;
	}
	auto &transform = scene_.FindObject(box_)->transform;
	const auto yaw = DirectX::XMQuaternionRotationAxis(
	    DirectX::XMVectorSet(0, 1, 0, 0), DirectX::XMConvertToRadians(previewYaw_));
	DirectX::XMStoreFloat4(&transform.rotation,
	                       DirectX::XMQuaternionMultiply(
	                           DirectX::XMLoadFloat4(&initialBoxTransform_.rotation), yaw));
	const auto &position = transform.translation;
	ImGui::Text("箱の位置  (%.2f, %.2f, %.2f)", double(position.x), double(position.y), double(position.z));
	ImGui::TextWrapped("黄色い箱は parentId で親を参照しています。親の回転を変えると、子のローカル座標を変えなくても位置が動きます。");
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
