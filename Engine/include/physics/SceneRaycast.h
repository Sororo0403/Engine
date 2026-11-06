#pragma once
#include "math/Ray.h"
#include "physics/RaycastHit.h"
#include "physics/RaycastOptions.h"
class Scene;

namespace SceneRaycast {
static constexpr float kMaximumDistance = 1000000;

/// <summary>
/// 有効なColliderをワールド行列で変形した箱と、レイの最も近い交点を求める
/// </summary>
/// <param name="scene">現在の姿勢を持つシーン。描画の表示設定やRigid Bodyの有無には依存しない</param>
/// <param name="ray">有限の始点・0でない方向・0以上1000000以下の距離。方向は内部で単位化する</param>
/// <param name="hit">成功時だけ設定する結果。同距離は小さいIDを優先する</param>
/// <param name="options">対象レイヤー・Trigger・除外ID。初期値ではTriggerを除く全レイヤー</param>
/// <returns>true:問い合わせ成功。交点なしはobjectIdが0 false:入力・形状・親子関係が無効</returns>
/// <remarks>回転・非一様拡縮・負の拡縮・せん断に対応する。箱の内部からは距離0、始点、レイと反対向きの法線を返す</remarks>
bool Cast(const Scene &scene, const Ray &ray, RaycastHit &hit,
          const RaycastOptions &options = {});
} // namespace SceneRaycast
