#pragma once

#include "graphics/camera/CameraPose.h"
#include "physics/CollisionWorld.h"
#include "scene/SceneObjectId.h"
#include <map>
#include <set>
#include <string>
#include <vector>

class Scene;
struct Transform;
struct FollowCameraSettings;
struct SceneWorldTransform;

/// <summary>
/// Camera物体と、視点切り替え中に描画へ渡す補間姿勢
/// </summary>
struct CameraTransitionView {
	SceneObjectId camera;
	CameraPose pose;
};

namespace SceneCameraUtils {
/// <summary>
/// 追従カメラの更新中、同じ障害物を調べるカメラ間で衝突形状を共有する
/// </summary>
/// <remarks>物体の有効状態・親子関係・Collider設定が変わらない更新区間ごとに作る</remarks>
class FollowObstacleCache {
  public:
	explicit FollowObstacleCache(const Scene &scene) : scene_(scene) {}

	/// <summary>
	/// 注視点からカメラまでをSweepし、障害物の手前へカメラを寄せる
	/// </summary>
	/// <param name="id">補正するFollow Cameraの物体ID</param>
	/// <param name="pose">補間済みの描画姿勢。成功時は位置だけ更新する</param>
	/// <returns>true:補正成功または回避無効 false:形状や姿勢が無効</returns>
	bool AvoidObstacles(SceneObjectId id, CameraPose &pose);

	/// <summary>
	/// 姿勢を変更した物体が障害物かその祖先なら、保存済みの衝突形状を破棄する
	/// </summary>
	void TransformChanged(SceneObjectId id);

  private:
	const CollisionWorld *GetWorld(SceneObjectId target);

	const Scene &scene_;
	SceneActivity::States active_;
	SceneHierarchy::WorldTransforms transforms_;
	std::vector<SceneObjectId> ignored_;
	std::set<SceneObjectId> obstacleAncestors_;
	std::map<SceneObjectId, CollisionWorld> worlds_;
};

/// <summary>
/// 2つの描画姿勢の位置を直線補間し、向きを最短の回転で補間する
/// </summary>
/// <param name="from">切り替え元</param>
/// <param name="to">切り替え先</param>
/// <param name="fraction">0から1までの補間割合</param>
/// <param name="result">成功時だけ設定する補間姿勢</param>
/// <returns>true:成功 false:有効な描画姿勢を求められない</returns>
bool BlendPose(const CameraPose &from, const CameraPose &to, float fraction,
               CameraPose &result);

/// <summary>
/// 有効なPrimary Cameraを取得する。固定ビューの場合は0を返す
/// </summary>
/// <param name="scene">現在の候補シーン</param>
/// <returns>描画に使う物体ID。該当なしは0</returns>
SceneObjectId FindPrimary(const Scene &scene);

/// <summary>
/// 候補シーン内のPrimary指定を一つに更新する。0は固定ビューへの切り替え
/// </summary>
/// <param name="scene">変更するシーン</param>
/// <param name="id">有効なCameraを持つ物体。0は全Primary指定を解除する</param>
/// <returns>true:成功 false:対象が不在、無効、またはCameraなし</returns>
bool SetPrimary(Scene &scene, SceneObjectId id);

/// <summary>
/// Transformの位置と回転から、+Zを前、+Yを上とする描画姿勢を作る
/// </summary>
/// <param name="transform">カメラ物体の姿勢。拡大率は描画姿勢へ適用しない</param>
/// <returns>ワールド座標のカメラ姿勢。入力の検証は呼び出し側で行う</returns>
CameraPose FromTransform(const Transform &transform);

/// <summary>
/// 親子合成後の位置と、拡縮やせん断を含めない向きからカメラ姿勢を作る
/// </summary>
/// <param name="world">有効なワールド変形</param>
/// <returns>ワールド座標のカメラ姿勢</returns>
CameraPose FromWorldTransform(const SceneWorldTransform &world);

/// <summary>
/// ワールドのカメラ姿勢を親に対するローカル位置・回転へ変換する
/// </summary>
/// <param name="scene">変更するシーン</param>
/// <param name="id">既存の物体ID</param>
/// <param name="pose">有効なワールドのカメラ姿勢</param>
/// <returns>true:成功 false:姿勢や親子関係が無効。失敗時は現在値を保持する</returns>
bool ApplyWorldPose(Scene &scene, SceneObjectId id, const CameraPose &pose);

/// <summary>
/// 親子関係と追従対象の両方を依存関係として、全物体の更新順を作る
/// </summary>
/// <param name="scene">読み取り専用のシーン</param>
/// <param name="order">成功時だけ置き換える、全物体のID一覧</param>
/// <param name="error">失敗理由。成功時は空</param>
/// <param name="invalidObject">指定時は問題の物体ID。成功時は0</param>
/// <returns>true:成功 false:設定・参照・依存関係が無効</returns>
bool GetUpdateOrder(const Scene &scene, std::vector<SceneObjectId> &order,
                    std::string &error, SceneObjectId *invalidObject = nullptr);

/// <summary>
/// 親子関係と追従を依存順に解決した、開始時のシーンを作る
/// </summary>
/// <param name="source">変更しない編集シーン</param>
/// <param name="result">成功時だけ物体を置き換える出力先</param>
/// <param name="error">失敗理由。成功時は空</param>
/// <param name="invalidObject">指定時は問題の物体ID。成功時は0</param>
/// <returns>true:成功 false:設定・参照・姿勢が無効。物理演算は実行しない</returns>
bool ResolveInitialScene(const Scene &source, Scene &result, std::string &error,
                         SceneObjectId *invalidObject = nullptr);

/// <summary>
/// 指定物体の祖先と追従先だけを解決し、全物体を保持した開始姿勢のシーンを作る
/// </summary>
/// <param name="source">変更しない編集シーン。障害物の形状も参照する</param>
/// <param name="id">開始姿勢を解決する既存物体ID。Camera以外も指定できる</param>
/// <param name="result">成功時だけ置き換える独立したシーン</param>
/// <returns>true:成功 false:対象・親子関係・依存する追従設定・姿勢が無効。無関係な未設定Follow Cameraは解決しない</returns>
bool ResolveInitialDependencies(const Scene &source, SceneObjectId id,
                                Scene &result);

/// <summary>
/// 追従対象の位置とワールド座標のオフセットから、目標の描画姿勢を作る
/// </summary>
/// <param name="target">対象のワールド位置</param>
/// <param name="settings">追従設定。補間はこの関数では行わない</param>
/// <returns>追従先に対するカメラの目標姿勢</returns>
CameraPose MakeFollowPose(const DirectX::XMFLOAT3 &target,
                          const FollowCameraSettings &settings);

/// <summary>
/// 注視点からカメラまで余裕幅を持つボックスをSweepし、静的なColliderの手前へカメラを寄せる
/// </summary>
/// <param name="scene">ColliderとFollow Cameraの設定を持つシーン</param>
/// <param name="id">補正するFollow Cameraの物体ID</param>
/// <param name="pose">補間済みの描画姿勢。成功時は位置だけ更新し、注視点・上方向は保持する</param>
/// <returns>true:補正成功または回避無効 false:注視点の余裕幅が障害物内、形状や姿勢が無効。失敗時はposeを保持する</returns>
/// <remarks>追従先・Character Controller・Follow CameraのColliderを除く。表示状態には依存しない</remarks>
bool AvoidFollowObstacles(const Scene &scene, SceneObjectId id,
                          CameraPose &pose);

/// <summary>
/// 描画姿勢をTransformの位置と回転へ反映する。拡大率は保持する
/// </summary>
/// <param name="pose">反映する位置・注視点・上方向</param>
/// <param name="transform">成功時に更新するTransform。失敗時は保持する</param>
/// <returns>true:成功 false:描画姿勢が無効</returns>
bool ApplyPose(const CameraPose &pose, Transform &transform);

/// <summary>
/// 親子関係と追従を解決した、カメラ表示用のワールド位置・向きを取得する。シーンは変更しない
/// </summary>
/// <param name="scene">Cameraと追従先が存在するシーン</param>
/// <param name="id">Cameraを持つ物体ID</param>
/// <param name="transform">成功時に置き換える表示用のワールドSRT。拡縮は単位値。描画物体の合成には使用しない</param>
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
/// 全Follow Cameraを検証し、親子関係と追従対象の更新順からCameraだけを列挙する
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
