#pragma once

#include "scene/SceneObjectId.h"
#include <DirectXMath.h>

/// <summary>
/// 確定済みの主人公の再生状態。位置はワールド座標、鉛直速度は上向きを正とする毎秒の値
/// </summary>
struct CharacterRuntimeState {
	SceneObjectId objectId = 0;
	DirectX::XMFLOAT3 position{};
	float verticalVelocity = 0.0f;
	bool grounded = false;
};
