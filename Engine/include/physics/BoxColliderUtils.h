#pragma once

#include "math/Transform.h"
#include "physics/AxisAlignedBox.h"
#include "physics/BoxCollider.h"

namespace BoxColliderUtils {

/// <summary>
/// 中心と半分の大きさが有限で、各軸の大きさが正か確認する
/// </summary>
/// <param name="collider">確認する衝突形状</param>
/// <returns>true:有効 false:無効</returns>
bool IsValid(const BoxCollider &collider);

/// <summary>
/// 回転・拡縮・移動を反映し、形状を囲むワールド座標のボックスを作る
/// </summary>
/// <param name="collider">ローカル座標の衝突形状</param>
/// <param name="transform">物体の変形情報</param>
/// <param name="bounds">成功時に設定するワールド座標のボックス</param>
/// <returns>true:成功 false:設定が無効、または有効な大きさを表現できない。失敗時は出力を保持する</returns>
bool GetWorldBounds(const BoxCollider &collider, const Transform &transform,
                    AxisAlignedBox &bounds);

} // namespace BoxColliderUtils
