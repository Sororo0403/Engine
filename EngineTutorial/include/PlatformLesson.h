#pragma once

#include "scene/SceneObjectId.h"
#include <DirectXMath.h>

class Scene;

/// <summary>
/// 箱のジャンプと、段差のある足場への着地を練習する
/// </summary>
class PlatformLesson {
  public:
	void Initialize(Scene &scene);
	void Reset(Scene &scene);
	void Update(Scene &scene, const DirectX::XMFLOAT2 &direction,
	            bool jumpPressed, float seconds);

	const DirectX::XMFLOAT3 &GetPosition(const Scene &scene) const;
	bool IsGrounded() const;

  private:
	void Step(DirectX::XMFLOAT3 &position, const DirectX::XMFLOAT2 &direction,
	          float seconds);

	SceneObjectId player_ = 0;
	float verticalSpeed_ = 0.f;
	bool grounded_ = false;
};
