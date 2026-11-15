#pragma once

#include "scene/SceneObjectId.h"
#include <DirectXMath.h>

class Scene;

/// <summary>
/// 箱のジャンプと、段差のある足場への着地を練習する
/// </summary>
class PlatformLesson {
  public:
	enum class Mode { PRACTICE, COURSE };
	enum class State { READY, RUNNING, CLEARED };

	void Initialize(Scene &scene, Mode mode);
	void Reset(Scene &scene);
	void Update(Scene &scene, const DirectX::XMFLOAT2 &direction,
	            bool jumpPressed, float seconds);

	const DirectX::XMFLOAT3 &GetPosition(const Scene &scene) const;
	bool IsGrounded() const;
	State GetState() const;
	float GetElapsed() const;
	int GetFalls() const;

  private:
	void Respawn(Scene &scene);
	bool Step(DirectX::XMFLOAT3 &position, const DirectX::XMFLOAT2 &direction,
	          float seconds);

	SceneObjectId player_ = 0;
	Mode mode_ = Mode::PRACTICE;
	State state_ = State::READY;
	float verticalSpeed_ = 0.f;
	float elapsed_ = 0.f;
	int falls_ = 0;
	bool grounded_ = false;
};
