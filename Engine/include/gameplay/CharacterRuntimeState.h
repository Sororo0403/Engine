#pragma once

#include "scene/SceneObjectId.h"
#include <DirectXMath.h>
#include <cstdint>
#include <optional>

/// <summary>
/// 確定済みの主人公の再生状態。位置はワールド座標、鉛直速度は上向きを正とする毎秒の値。
/// 落下やTrigger Actionsによる復帰は今回の再生開始以降の回数と、直近の復帰先を保持する
/// </summary>
struct CharacterRuntimeState {
	SceneObjectId objectId = 0;
	DirectX::XMFLOAT3 position{};
	float verticalVelocity = 0.0f;
	bool grounded = false;
	std::uint64_t respawnCount = 0;
	std::optional<DirectX::XMFLOAT3> lastRespawnPosition;

	// 接地中の足元のCollider。空中、または足元にColliderがない場合は0
	SceneObjectId groundObjectId = 0;
};
