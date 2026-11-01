#pragma once

#include "graphics/camera/CameraPose.h"
#include "scene/SceneObjectId.h"

class Scene;
struct Transform;
struct FollowCameraSettings;

namespace SceneCameraUtils {
/// <summary>
/// Transformの位置と回転から、+Zを前、+Yを上とする描画姿勢を作る
/// </summary>
/// <param name="transform">カメラ物体の姿勢。拡大率は描画姿勢へ適用しない</param>
/// <returns>ワールド座標のカメラ姿勢。入力の検証は呼び出し側で行う</returns>
CameraPose FromTransform(const Transform &transform);

/// <summary>
/// 追従対象の位置とワールド座標のオフセットから、目標の描画姿勢を作る
/// </summary>
/// <param name="target">対象のワールド位置</param>
/// <param name="settings">追従設定。補間はこの関数では行わない</param>
/// <returns>追従先に対するカメラの目標姿勢</returns>
CameraPose MakeFollowPose(const DirectX::XMFLOAT3 &target,
                          const FollowCameraSettings &settings);

/// <summary>
/// 描画姿勢をTransformの位置と回転へ反映する。拡大率は保持する
/// </summary>
/// <param name="pose">反映する位置・注視点・上方向</param>
/// <param name="transform">成功時に更新するTransform。失敗時は保持する</param>
/// <returns>true:成功 false:描画姿勢が無効</returns>
bool ApplyPose(const CameraPose &pose, Transform &transform);

/// <summary>
/// 指定したCameraの開始姿勢を求める。Follow Cameraは追従先から計算する
/// </summary>
/// <param name="scene">参照する編集シーン。値は変更しない</param>
/// <param name="id">Primaryの指定によらず、姿勢を求めるCameraのID</param>
/// <param name="pose">成功時に更新する描画姿勢。失敗時は保持する</param>
/// <returns>true:成功 false:Cameraなし、無効な姿勢・設定、または追従先なし</returns>
bool GetInitialPose(const Scene &scene, SceneObjectId id, CameraPose &pose);
/// <summary>
/// 位置と視線方向に合う追従オフセットを求める。元の注視距離を基準に設定範囲内の注視点を選ぶ
/// </summary>
/// <param name="pose">合わせる位置と視線方向。上方向はFollow Cameraと同じワールドYを基準とする</param>
/// <param name="target">追従対象のワールド位置</param>
/// <param name="settings">成功時に更新する設定。対象ID・追従速度と、失敗時の値は保持する</param>
/// <returns>true:成功 false:姿勢が表現できない、または設定・位置が無効</returns>
bool MatchFollowPose(const CameraPose &pose, const DirectX::XMFLOAT3 &target,
                     FollowCameraSettings &settings);
} // namespace SceneCameraUtils
