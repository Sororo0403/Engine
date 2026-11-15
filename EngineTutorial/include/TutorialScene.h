#pragma once
#include "scene/IScene.h"
#include "scene/Scene.h"
#include "scene/SceneRenderer.h"
#include <cstdint>
#include <memory>

struct EngineContext;
class ImGuiSystem;

/// <summary>
/// 各章の実験と説明を表示するチュートリアルシーン
/// </summary>
class TutorialScene final : public IScene {
  public:
	explicit TutorialScene(const EngineContext &context);
	~TutorialScene() override;

	bool Initialize() override;
	bool Update(float seconds) override;
	void Draw() const override;
	void Finalize() override;

  private:
	enum class Chapter { STARTUP, BOX };

	void DrawStartupLesson() const;
	void DrawBoxLesson();
	bool DrawPreview();

	ImGuiSystem &ui_;
	RenderSystem &render_;
	Scene scene_;
	SceneRenderer renderer_;
	std::unique_ptr<RenderTexture> preview_;
	SceneObjectId box_ = 0;
	Transform initialBoxTransform_;
	Chapter chapter_ = Chapter::STARTUP;
	float previewYaw_ = 0;
	bool previewVisible_ = false;

	float elapsed_ = 0;
	std::uint64_t updates_ = 0;
	mutable std::uint64_t draws_ = 0;
};
