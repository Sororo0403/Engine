#pragma once
#include "math/Ray.h"
#include "physics/RaycastHit.h"
#include "physics/RaycastOptions.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
class Scene;

namespace SceneRaycast {
static constexpr float kMaximumDistance = 1000000;

/// <summary>
/// 姿勢と有効状態を共有して、同じシーンに複数のレイを飛ばす
/// </summary>
/// <remarks>物体の追加・削除、親子関係、姿勢、有効状態の変更後は再構築する</remarks>
class Prepared {
  public:
	bool Build(const Scene &scene);
	bool Cast(const Ray &ray, RaycastHit &hit,
	          const RaycastOptions &options = {}) const;

  private:
	const Scene *scene_ = nullptr;
	SceneHierarchy::WorldTransforms transforms_;
	SceneActivity::States active_;
};

/// <summary>
/// 有効なColliderをワールド行列で変形した箱・球・カプセルと、レイの最も近い交点を求める
/// </summary>
/// <param name="scene">現在の姿勢を持つシーン。描画の表示設定やRigid Bodyの有無には依存しない</param>
/// <param name="ray">有限の始点・0でない方向・0以上1000000以下の距離。方向は内部で単位化する</param>
/// <param name="hit">成功時だけ設定する結果。同距離は小さいIDを優先する</param>
/// <param name="options">対象レイヤー・Trigger・除外ID。初期値ではTriggerを除く全レイヤー</param>
/// <returns>true:問い合わせ成功。交点なしはobjectIdが0 false:入力・形状・親子関係が無効</returns>
/// <remarks>箱は回転・非一様拡縮・負の拡縮・せん断に対応し、球・カプセルは一様拡縮に対応する。形状の内部からは距離0、始点、レイと反対向きの法線を返す</remarks>
bool Cast(const Scene &scene, const Ray &ray, RaycastHit &hit,
          const RaycastOptions &options = {});
/// <summary>
/// ワールド座標の球をレイの方向へ移動し、最初に接触するColliderを求める
/// </summary>
/// <param name="scene">現在のColliderの姿勢を持つシーン</param>
/// <param name="ray">球の始点・0でない方向・0以上1000000以下の距離</param>
/// <param name="radius">ワールド単位の球の半径</param>
/// <param name="hit">成功時だけ設定する結果。positionは接触時の球の中心。同距離は小さいIDを優先する</param>
/// <param name="options">対象レイヤー・Trigger・除外ID</param>
/// <returns>true:問い合わせ成功。命中なしはobjectIdが0 false:入力・形状・親子関係が無効</returns>
/// <remarks>開始時に重なる場合は、距離0と移動方向の逆向きの法線を返す</remarks>
bool CastSphere(const Scene &scene, const Ray &ray, float radius,
                RaycastHit &hit, const RaycastOptions &options = {});
} // namespace SceneRaycast
