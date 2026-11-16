#pragma once

#include "physics/AxisAlignedBox.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace StageDecorationLayout {
enum class Role { WIND_GUIDE, LANDING_GUIDE, SCENERY, BACKDROP };

struct WindArea {
	AxisAlignedBox bounds;
	DirectX::XMFLOAT3 velocity;
};

struct Placement {
	Role role;
	DirectX::XMFLOAT3 position;
	float yaw;
};

/// <summary>
/// 進行順の足場と風の範囲から、装飾を置ける位置と向きを選ぶ
/// </summary>
/// <param name="route">着地面を最大Yに持つ足場を進行順に並べた一覧</param>
/// <param name="windAreas">目印が必要な風の範囲と速度</param>
/// <param name="occupied">立入禁止領域や既存の地上物の境界</param>
/// <param name="footprints">役割ごとの装飾の幅・高さ・奥行きの最大値</param>
/// <param name="seed">配置を再現するためのシード</param>
/// <returns>優先度が高い順の配置結果</returns>
std::vector<Placement>
Generate(std::span<const AxisAlignedBox> route,
         std::span<const WindArea> windAreas,
         std::span<const AxisAlignedBox> occupied,
         const std::array<DirectX::XMFLOAT3, 4> &footprints,
         std::uint32_t seed);
} // namespace StageDecorationLayout
