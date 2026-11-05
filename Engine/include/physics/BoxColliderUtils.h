#pragma once

#include "math/Transform.h"
#include "physics/AxisAlignedBox.h"
#include "physics/BoxCollider.h"
#include "scene/SceneObjectId.h"

#include <span>

class Scene;

namespace BoxColliderUtils {

/// <summary>
/// 形状が有限で各軸の大きさが正、かつ衝突レイヤーが有効か確認する
/// </summary>
/// <param name="collider">確認する衝突形状</param>
/// <returns>true:有効 false:無効</returns>
bool IsValid(const BoxCollider &collider);

/// <summary>
/// モデルなどのローカル座標の頂点を囲む衝突形状を作る
/// </summary>
/// <param name="points">有限のローカル座標の頂点</param>
/// <param name="collider">成功時に形状を設定する。衝突レイヤーとマスクは保持する</param>
/// <param name="minimumHalfExtent">厚みのない軸にも設定する半分の大きさ</param>
/// <returns>true:成功 false:頂点なし、または値が無効。失敗時は出力を保持する</returns>
bool FitToPoints(std::span<const DirectX::XMFLOAT3> points,
                 BoxCollider &collider, float minimumHalfExtent = 0.001f);

/// <summary>
/// 回転・拡縮・移動を反映し、形状を囲むワールド座標のボックスを作る
/// </summary>
/// <param name="collider">ローカル座標の衝突形状</param>
/// <param name="transform">物体の変形情報</param>
/// <param name="bounds">成功時に設定するワールド座標のボックス</param>
/// <returns>true:成功 false:設定が無効、または有効な大きさを表現できない。失敗時は出力を保持する</returns>
bool GetWorldBounds(const BoxCollider &collider, const Transform &transform,
                    AxisAlignedBox &bounds);

/// <summary>
/// 親子合成などのワールド行列で形状の8頂点を変換し、AABBを作る
/// </summary>
/// <param name="collider">ローカル座標の衝突形状</param>
/// <param name="world">せん断を含められる有限な可逆アフィン行列</param>
/// <param name="bounds">成功時だけ置き換えるワールド座標のボックス</param>
/// <returns>true:成功 false:形状・行列・変換後の大きさが無効</returns>
bool GetWorldBounds(const BoxCollider &collider,
                    const DirectX::XMFLOAT4X4 &world, AxisAlignedBox &bounds);

/// <summary>
/// 物体の祖先を含めた変形で、指定したローカル形状のワールドAABBを求める
/// </summary>
/// <param name="collider">物体に属するローカル形状</param>
/// <param name="scene">物体と祖先を取得するシーン</param>
/// <param name="id">既存の物体ID</param>
/// <param name="bounds">成功時だけ置き換えるワールドAABB</param>
/// <returns>true:成功 false:親子関係・形状・変形が無効</returns>
bool GetWorldBounds(const BoxCollider &collider, const Scene &scene,
                    SceneObjectId id, AxisAlignedBox &bounds);
} // namespace BoxColliderUtils
