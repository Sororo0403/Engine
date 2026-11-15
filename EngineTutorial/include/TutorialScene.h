#pragma once
#include "scene/IScene.h"
#include <cstdint>

/// <summary>
/// 各章の実験と説明を表示するチュートリアルシーン
/// </summary>
class TutorialScene final : public IScene {
  public:
	bool Initialize() override;
	bool Update(float seconds) override;
	void Draw() const override;
	void Finalize() override;

  private:
	void DrawStartupLesson() const;

	float elapsed_ = 0;
	std::uint64_t updates_ = 0;
	mutable std::uint64_t draws_ = 0;
};
