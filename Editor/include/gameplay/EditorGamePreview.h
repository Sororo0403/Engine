#pragma once

#include "graphics/camera/CameraPose.h"
#include "graphics/camera/CameraSettings.h"
#include "scene/Scene.h"

/// <summary>
/// 物理演算を実行せず、編集シーンからGameビューの開始時表示を準備する
/// </summary>
class EditorGamePreview {
  public:
	/// <summary>
	/// Follow Cameraの開始姿勢とPrimary Cameraを解決し、独立した描画用シーンを作る
	/// </summary>
	/// <param name="source">編集シーン。値は変更しない。Character ControllerとColliderの検証は行わない</param>
	/// <param name="fallback">Primary Cameraがない場合のSceneビューの姿勢</param>
	/// <returns>true:準備成功 false:カメラ設定または複製が無効。失敗時は過去の表示データを破棄する</returns>
	bool Prepare(const Scene &source, const CameraPose &fallback);

	/// <summary>
	/// 表示用シーンを空にし、カメラを初期値へ戻す
	/// </summary>
	void Reset();

	/// <summary>
	/// 描画に使用する独立したシーンを取得する。更新してもシーン本体のアドレスは変わらない
	/// </summary>
	/// <returns>物理演算を進めていない表示用シーン</returns>
	const Scene &GetScene() const;

	/// <summary>
	/// Gameビューの描画姿勢を取得する
	/// </summary>
	/// <returns>準備済みの姿勢。準備失敗時は空の描画に使用する初期値</returns>
	const CameraPose &GetPose() const;

	/// <summary>
	/// Gameビューの投影設定を取得する
	/// </summary>
	/// <returns>準備済みの投影設定。準備失敗時は初期値</returns>
	const CameraSettings &GetSettings() const;

  private:
	Scene scene_;
	CameraPose pose_{};
	CameraSettings settings_{};
};
