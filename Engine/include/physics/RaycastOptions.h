#pragma once
#include "physics/CollisionFilter.h"
#include "scene/SceneObjectId.h"
#include <span>

/// <summary>
/// レイの対象レイヤー・Triggerの扱い・除外する物体を指定する
/// </summary>
struct RaycastOptions {
	// Colliderの所属レイヤーだけを選ぶ。Collider同士の相互マスクは使用しない
	std::uint32_t layerMask = CollisionFilter::kAllLayers;
	bool includeTriggers = false;
	std::span<const SceneObjectId> ignored{};
};
