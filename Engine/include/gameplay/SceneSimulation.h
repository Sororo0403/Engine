#pragma once

#include "gameplay/CharacterController.h"
#include "graphics/camera/CameraPose.h"
#include "physics/CollisionWorld.h"
#include "scene/Scene.h"
#include <string>

/// <summary>
/// 編集シーンから独立した再生用シーンで、移動・追従・復帰を実行する
/// </summary>
class SceneSimulation {
  public:
	/// <summary>
	/// コンポーネントを検証し、再生用シーンを複製する。失敗時は現在の再生状態を保持する
	/// </summary>
	/// <param name="source">変更しない編集シーン</param>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <param name="fallbackPose">Primary Cameraがない場合に使用する固定の描画姿勢</param>
	/// <returns>true:再生開始 false:追加済みコンポーネントの設定や配置、または描画姿勢が無効</returns>
	bool Start(const Scene &source, std::string &error,
	           const CameraPose &fallbackPose = CameraPose{});

	/// <summary>
	/// 再生を終了して複製データを破棄する
	/// </summary>
	void Stop();

	/// <summary>
	/// 再生用シーンを入力と時間から更新する
	/// </summary>
	/// <param name="seconds">有限な0以上の秒数</param>
	/// <param name="input">ワールドX/Z方向の移動とジャンプ</param>
	/// <returns>true:更新成功 false:未再生または更新失敗</returns>
	bool Update(float seconds, const CharacterInput &input);

	/// <summary>
	/// 再生状態を確認する
	/// </summary>
	/// <returns>true:再生中 false:停止中</returns>
	bool IsPlaying() const {
		return playing_;
	}

	/// <summary>
	/// Rendererから参照できる、アドレスが変わらない再生用シーンを取得する
	/// </summary>
	/// <returns>編集データとは独立したシーン</returns>
	const Scene &GetScene() const {
		return scene_;
	}

	/// <summary>
	/// 追従カメラ、または開始時に指定した固定カメラの姿勢を取得する
	/// </summary>
	/// <returns>再生時の描画姿勢</returns>
	const CameraPose &GetCameraPose() const {
		return pose_;
	}

	/// <summary>
	/// 描画カメラの投影設定を取得する。Primary Cameraがなければ標準設定を返す
	/// </summary>
	/// <returns>再生中の縦視野角と表示距離</returns>
	const CameraSettings &GetCameraSettings() const {
		return cameraSettings_;
	}

  private:
	Scene scene_;
	CollisionWorld world_;
	CharacterController controller_;
	SceneObjectId character_ = 0, camera_ = 0, follow_ = 0;
	DirectX::XMFLOAT3 spawn_{};
	CameraPose pose_{};
	CameraPose followPose_{};
	CameraSettings cameraSettings_{};
	bool playing_ = false;
};
