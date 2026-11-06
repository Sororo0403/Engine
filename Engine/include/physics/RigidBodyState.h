#pragma once

#include "math/Transform.h"
#include "scene/SceneObjectId.h"

/// <summary>
/// 物理演算で確定した物体の姿勢・速度と、現在動作しているかを保持する
/// </summary>
struct RigidBodyState {
	SceneObjectId object = 0;
	// Colliderの中心ではなく、元の物体の原点。拡大率は登録時の値を保持する
	Transform transform{};
	DirectX::XMFLOAT3 linearVelocity{};
	DirectX::XMFLOAT3 angularVelocity{};
	bool active = false;
};
