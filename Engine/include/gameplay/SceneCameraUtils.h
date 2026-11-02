#pragma once

#include "graphics/camera/CameraPose.h"
#include "scene/SceneObjectId.h"
#include <string>
#include <vector>

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
/// 追従の連鎖を解決した開始Transformを取得する。シーンは変更しない
/// </summary>
/// <param name="scene">Cameraと追従先が存在するシーン</param>
/// <param name="id">Cameraを持つ物体ID</param>
/// <param name="transform">成功時に置き換えるTransform。失敗時は保持する</param>
/// <returns>true:解決成功 false:設定・対象・連鎖・姿勢が無効</returns>
bool GetInitialTransform(const Scene &scene, SceneObjectId id,
                         Transform &transform);

/// <summary>
/// 指定したCameraの開始姿勢を求める。Follow Cameraは追従先の連鎖を解決する
/// </summary>
/// <param name="scene">参照する編集シーン。値は変更しない</param>
/// <param name="id">Primaryの指定によらず、姿勢を求めるCameraのID</param>
/// <param name="pose">成功時に更新する描画姿勢。失敗時は保持する</param>
/// <returns>true:成功 false:Cameraなし、無効な姿勢・設定、または追従先なし</returns>
bool GetInitialPose(const Scene &scene, SceneObjectId id, CameraPose &pose);

/// <summary>
/// 追従先の変更後に、対象の不在や自己参照・循環が生じないか確認する。
/// 位置・レンズ・追従オフセットの検証は行わない
/// </summary>
/// <param name="scene">参照するシーン。値は変更しない</param>
/// <param name="id">追従元の物体ID。シーン内に存在すること</param>
/// <param name="target">設定する追従先。0は追従先の解除</param>
/// <returns>true:参照を設定可能 false:物体なし、または追従先から続く参照が不在・循環</returns>
bool CanSetFollowTarget(const Scene &scene, SceneObjectId id,
                        SceneObjectId target);

/// <summary>
/// 全Follow Cameraを検証し、追従先のCameraが先に現れる更新順を作る
/// </summary>
/// <param name="scene">検証するシーン。値は変更しない</param>
/// <param name="order">成功時に置き換えるCameraのID一覧。失敗時は保持する</param>
/// <param name="error">不正な設定・対象・循環の失敗理由と、該当する物体の名前・ID。成功時は空</param>
/// <param name="invalidObject">指定時は問題の物体IDを設定する。循環では循環内の1物体。成功時は0</param>
/// <returns>true:成功 false:追従設定または依存関係が無効</returns>
bool GetFollowOrder(const Scene &scene, std::vector<SceneObjectId> &order,
                    std::string &error, SceneObjectId *invalidObject = nullptr);

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
