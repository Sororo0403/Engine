#include "TutorialScene.h"
#include "BoxLesson.h"
#include "CameraLesson.h"
#include "MovementLesson.h"
#include "core/EngineContext.h"
#include "core/Logger.h"
#include "graphics/render/RenderSystem.h"
#include "graphics/render/RenderTexture.h"
#include "graphics/ui/ImGuiSystem.h"
#include "input/InputSystem.h"
#include <algorithm>
#include <imgui.h>

namespace {

constexpr ImVec4 kAccent{0.44f, 0.82f, 0.73f, 1.0f};
constexpr std::array kUpdateRates{30, 60, 120};
constexpr CameraPose kFixedCamera{{4.4f, 3.4f, -7.0f}, {0, 0.4f, 0}};

} // namespace

TutorialScene::TutorialScene(const EngineContext &context)
    : ui_(context.ui), render_(context.render), input_(context.input),
      renderer_(scene_, context.models, context.render) {
}

TutorialScene::~TutorialScene() = default;

bool TutorialScene::Initialize() {
	PrepareBoxLesson();
	LightingSettings lighting;
	lighting.ambient = 0.36f;
	if (!renderer_.Initialize() || !renderer_.SetLighting(lighting) ||
	    !renderer_.SetCameraPose(kFixedCamera)) {
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
		const auto previous = chapter_;
		if (ImGui::Button("1  起動")) {
			chapter_ = Chapter::STARTUP;
		}
		ImGui::SameLine();
		if (ImGui::Button("2  箱の表示")) {
			chapter_ = Chapter::BOX;
		}
		ImGui::SameLine();
		if (ImGui::Button("3  入力と移動")) {
			chapter_ = Chapter::MOVEMENT;
		}
		ImGui::SameLine();
		if (ImGui::Button("4  カメラ")) {
			chapter_ = Chapter::CAMERA;
		}
		ImGui::SameLine();
		if (ImGui::Button("5  ジャンプ")) {
			chapter_ = Chapter::PLATFORM;
		}
		if (chapter_ != previous) {
			ready = EnterChapter(previous);
		}
		ImGui::Separator();
		ImGui::PushID(static_cast<int>(chapter_));
		if (chapter_ == Chapter::STARTUP) {
			DrawStartupLesson();
		} else {
			const float previewWidth = ImGui::GetContentRegionAvail().x * 0.59f;
			if (ImGui::BeginChild("Preview", {previewWidth, 0}, ImGuiChildFlags_Borders)) {
				ready = DrawPreview() && ready;
			}
			ImGui::EndChild();
			ImGui::SameLine();
			if (ImGui::BeginChild("Lesson", {0, 0}, ImGuiChildFlags_Borders)) {
				if (chapter_ == Chapter::BOX) {
					DrawBoxLesson();
				} else if (chapter_ == Chapter::MOVEMENT) {
					DrawMovementLesson(seconds);
				} else if (chapter_ == Chapter::CAMERA) {
					ready = DrawCameraLesson(seconds) && ready;
				} else {
					ready = DrawPlatformLesson(seconds) && ready;
				}
			}
			ImGui::EndChild();
		}
		ImGui::PopID();
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
	return ready && (!previewVisible_ || renderer_.Update(preview_->GetWidth(), preview_->GetHeight()));
}

bool TutorialScene::EnterChapter(Chapter previous) {
	if (chapter_ == Chapter::PLATFORM) {
		scene_.Clear();
		platformLesson_.Initialize(scene_);
		return renderer_.SetCameraPose(MakeFollowCamera(platformLesson_.GetPosition(scene_)));
	}
	if (previous == Chapter::PLATFORM) {
		scene_.Clear();
		PrepareBoxLesson();
	}
	scene_.FindObject(box_)->transform = initialBoxTransform_;
	previewYaw_ = 0;
	return renderer_.SetCameraPose(kFixedCamera);
}

void TutorialScene::PrepareBoxLesson() {
	box_ = CreateLessonBox(scene_);
	initialBoxTransform_ = scene_.FindObject(box_)->transform;
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

void TutorialScene::DrawMovementLesson(float seconds) {
	ImGui::SeparatorText("第3章  入力と移動時間");
	ImGui::TextWrapped("WASDで緑の箱を動かします。D・AはX軸、W・SはZ軸の移動です。");
	auto &transform = scene_.FindObject(box_)->transform;
	if (ImGui::Button("箱を初期位置に戻す")) {
		transform = initialBoxTransform_;
	} else {
		UpdateFreeMovement(seconds);
	}
	ImGui::Text("箱の位置  X %.2f / Z %.2f",
	            double(transform.translation.x), double(transform.translation.z));
	ImGui::Text("前の更新から %.1f ms", double(seconds) * 1000.0);

	ImGui::SeparatorText("更新回数を変えて測る");
	ImGui::TextWrapped("Dを1秒間押した状態を、30・60・120回の更新で再現します。画面のフレームレートに関係なく同じ条件で比べられます。");
	if (ImGui::Button("1秒間の移動距離を比較")) {
		Keyboard keyboard;
		Keyboard::State keys{};
		keys[static_cast<std::size_t>(Key::D)] = true;
		keyboard.Update(keys);
		auto &distances = movementDistances_.emplace();
		for (std::size_t i = 0; i < kUpdateRates.size(); ++i) {
			Transform simulated;
			const int updates = kUpdateRates[i];
			for (int frame = 0; frame < updates; ++frame) {
				MoveLessonBox(simulated, keyboard, 1.f / static_cast<float>(updates));
			}
			distances[i] = simulated.translation.x;
		}
	}
	if (movementDistances_ && ImGui::BeginTable("Distances", 2, ImGuiTableFlags_BordersInnerH)) {
		ImGui::TableSetupColumn("1秒間の更新回数");
		ImGui::TableSetupColumn("Xの移動距離");
		ImGui::TableHeadersRow();
		for (std::size_t i = 0; i < kUpdateRates.size(); ++i) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%d 回", kUpdateRates[i]);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%.2f", double((*movementDistances_)[i]));
		}
		ImGui::EndTable();
	}

	ImGui::SeparatorText("C++を書き換える");
	ImGui::TextWrapped("src/MovementLesson.cpp の StepDistance() を開き、speed / 60.f を speed * seconds に変えてビルドします。");
	ImGui::TextWrapped("もう一度測定し、どの更新回数でも約3.00になるか確認してください。");
}

bool TutorialScene::DrawCameraLesson(float seconds) {
	ImGui::SeparatorText("第4章  カメラ");
	ImGui::TextWrapped("WASDで箱を動かし、同じ位置で追従を切り替えて、固定カメラとの違いを見比べます。");
	ImGui::Checkbox("箱を追いかける", &followCamera_);
	if (ImGui::Button("箱を初期位置に戻す")) {
		scene_.FindObject(box_)->transform = initialBoxTransform_;
	} else {
		UpdateFreeMovement(seconds);
	}

	const auto &target = scene_.FindObject(box_)->transform.translation;
	const auto pose = followCamera_ ? MakeFollowCamera(target) : kFixedCamera;
	ImGui::Text("位置     (%.2f, %.2f, %.2f)",
	            double(pose.position.x), double(pose.position.y), double(pose.position.z));
	ImGui::Text("注視点  (%.2f, %.2f, %.2f)",
	            double(pose.target.x), double(pose.target.y), double(pose.target.z));
	ImGui::SeparatorText("C++を書き換える");
	ImGui::TextWrapped("src/CameraLesson.cpp の offset.y を 8.f に変えてビルドすると、より上から見下ろします。次に offset.z を -12.f にして、箱までの距離を比べます。");
	ImGui::TextWrapped("CameraPose の position はカメラを置く位置、target は見る先です。追従時は毎フレーム箱の位置を基準に計算します。");
	return renderer_.SetCameraPose(pose);
}

bool TutorialScene::DrawPlatformLesson(float seconds) {
	ImGui::SeparatorText("第5章  ジャンプと着地");
	ImGui::TextWrapped("WASDで移動し、Spaceでジャンプします。足場の上面に着地できます。落ちた場合はスタートに戻ります。");
	if (ImGui::Button("スタートに戻る")) {
		platformLesson_.Reset(scene_);
	} else if (input_.IsActive() && !ui_.IsTextInputActive()) {
		const auto &keyboard = input_.GetKeyboard();
		platformLesson_.Update(scene_, ReadMovementDirection(keyboard),
		                       keyboard.IsPressed(Key::SPACE), seconds);
	}

	const auto &position = platformLesson_.GetPosition(scene_);
	ImGui::TextUnformatted(platformLesson_.IsGrounded() ? "地面にいる" : "空中にいる");
	ImGui::Text("箱の高さ  %.2f", double(position.y));
	ImGui::SeparatorText("C++を書き換える");
	ImGui::TextWrapped("src/PlatformLesson.cpp の kJumpSpeed を 7.5f に変更してビルドし、足場までの距離を比べます。次に kGravity を 18.f に変えて、落ちる速さを観察します。");
	ImGui::TextWrapped("Step() は重力で縦方向の速度を変え、下降中に箱の底が足場の上面を越えたときに着地させます。箱の位置はSceneのTransformなので、そのまま描画に反映されます。");
	return renderer_.SetCameraPose(MakeFollowCamera(position));
}

void TutorialScene::UpdateFreeMovement(float seconds) {
	if (!input_.IsActive() || ui_.IsTextInputActive()) {
		return;
	}

	auto &transform = scene_.FindObject(box_)->transform;
	MoveLessonBox(transform, input_.GetKeyboard(), seconds);
	transform.translation.x = std::clamp(transform.translation.x, -3.5f, 3.5f);
	transform.translation.z = std::clamp(transform.translation.z, -3.5f, 3.5f);
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
