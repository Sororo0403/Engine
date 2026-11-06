#pragma once

#include "math/Transform.h"
#include "physics/Collider.h"
#include "physics/RigidBodySettings.h"
#include "scene/SceneObjectId.h"
#include <optional>

/// <summary>
/// 物理ワールドへ登録する形状と、そのワールド姿勢を保持する
/// </summary>
struct RigidBodyDescription {
	SceneObjectId object = 0;
	Transform transform{};
	Collider collider{};
	// 未指定または無効化した設定は静的な障害物として扱う
	std::optional<RigidBodySettings> rigidBody;
	// シーンの再構成時に、継続する動的な物体の休止状態を引き継ぐ
	bool startSleeping = false;
};
