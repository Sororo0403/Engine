#pragma once

#include "animation/ModelAnimationData.h"

/// <summary>
/// 物体ごと・メッシュごとの骨と法線の変換。最後の1枠は重みなしの頂点に使用する
/// </summary>
struct SkinningConstants {
	static constexpr std::size_t kMatrixCount =
	    ModelAnimationData::kMaximumBones + 1;
	std::array<DirectX::XMFLOAT4X4, kMatrixCount> transforms{};
	std::array<DirectX::XMFLOAT4X4, kMatrixCount> normals{};
	std::uint32_t boneCount = 0;
	std::uint32_t padding[3]{};
};

static_assert(sizeof(SkinningConstants) == 32912);
