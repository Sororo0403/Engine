#pragma once

#include "gameplay/FollowCameraSettings.h"
#include "gameplay/OrbitCameraInput.h"
#include "graphics/camera/CameraPose.h"

/// <summary>
/// 編集用の追従設定を変更せず、再生中の旋回角と希望距離を保持する
/// </summary>
class FollowCameraOrbit {
  public:
	/// <summary>
	/// Camera OffsetとLook Offsetの差を初期姿勢とし、角度・距離の制限を適用する
	/// </summary>
	/// <param name="settings">有効な追従設定</param>
	/// <returns>true:成功 false:設定が無効。失敗時は状態を保持する</returns>
	bool Initialize(const FollowCameraSettings &settings);

	/// <summary>
	/// 1フレームの相対操作と時間に比例するスティック操作を適用する
	/// </summary>
	/// <param name="seconds">有限で0以上の経過秒数。スティックの積分は0.1秒まで</param>
	/// <param name="input">正のmouseYは見下ろす方向、正のstickYは見上げる方向</param>
	/// <param name="settings">初期化時と同じ有効な設定</param>
	/// <returns>true:成功 false:未初期化、入力または設定が無効。失敗時は状態を保持する。0秒では入力を消費しない</returns>
	bool Update(float seconds, const OrbitCameraInput &input,
	            const FollowCameraSettings &settings);

	/// <summary>
	/// 障害物回避前の、希望する距離と向きの追従姿勢を計算する
	/// </summary>
	/// <param name="target">有限な追従対象のワールド位置</param>
	/// <param name="settings">有効な追従設定</param>
	/// <returns>対象とLook Offsetを中心に旋回するカメラ姿勢</returns>
	CameraPose MakePose(const DirectX::XMFLOAT3 &target,
	                    const FollowCameraSettings &settings) const;

  private:
	double yaw_ = 0, pitch_ = 0, distance_ = 1;
	bool initialized_ = false;
};
