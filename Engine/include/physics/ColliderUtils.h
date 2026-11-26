#pragma once

#include "physics/AxisAlignedBox.h"
#include "physics/Collider.h"
#include "scene/SceneObjectId.h"

#include <span>

class Scene;

namespace ColliderUtils {

/// <summary>
/// 形状が有限で各軸の大きさが正、かつ衝突レイヤーが有効か確認する
/// </summary>
/// <param name="collider">確認する衝突形状</param>
/// <returns>true:有効 false:無効</returns>
bool IsValid(const Collider &collider);

/// <summary>
/// 選択した形状でモデルなどのローカル頂点を囲む。中心と寸法だけを置き換える
/// </summary>
/// <param name="points">有限のローカル座標の頂点</param>
/// <param name="collider">成功時に形状を設定する。衝突レイヤー・マスク・Trigger設定は保持する</param>
/// <param name="minimumHalfExtent">厚みのない軸にも設定する半分の大きさ</param>
/// <returns>true:成功 false:頂点なし、または値が無効。失敗時は出力を保持する</returns>
bool FitToPoints(std::span<const DirectX::XMFLOAT3> points, Collider &collider,
                 float minimumHalfExtent = 0.001f);

/// <summary>
/// 親子合成した形状の外接AABBを作る。箱はせん断に、球・カプセルは一様拡縮に対応する
/// </summary>
/// <param name="collider">ローカル座標の衝突形状</param>
/// <param name="world">せん断を含められる有限な可逆アフィン行列</param>
/// <param name="bounds">成功時だけ置き換えるワールド座標のボックス</param>
/// <returns>true:成功 false:形状・行列・変換後の大きさが無効</returns>
bool GetWorldBounds(const Collider &collider, const DirectX::XMFLOAT4X4 &world,
                    AxisAlignedBox &bounds);

/// <summary>
/// 物体の祖先を含めた変形で、指定したローカル形状のワールドAABBを求める
/// </summary>
/// <param name="collider">物体に属するローカル形状</param>
/// <param name="scene">物体と祖先を取得するシーン</param>
/// <param name="id">既存の物体ID</param>
/// <param name="bounds">成功時だけ置き換えるワールドAABB</param>
/// <returns>true:成功 false:親子関係・形状・変形が無効</returns>
bool GetWorldBounds(const Collider &collider, const Scene &scene,
                    SceneObjectId id, AxisAlignedBox &bounds);
} // namespace ColliderUtils
