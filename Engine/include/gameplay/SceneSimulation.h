#pragma once

#include "gameplay/CharacterController.h"
#include "gameplay/CharacterRuntimeState.h"
#include "graphics/camera/CameraPose.h"
#include "physics/CollisionWorld.h"
#include "physics/TriggerWorld.h"
#include "scene/Scene.h"
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

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
	/// <param name="invalidObject">指定時は問題の物体IDを設定する。成功時や物体に紐付かない失敗では0</param>
	/// <returns>true:再生開始 false:追加済みコンポーネントの設定や配置、または描画姿勢が無効</returns>
	bool Start(const Scene &source, std::string &error,
	           const CameraPose &fallbackPose = CameraPose{},
	           SceneObjectId *invalidObject = nullptr);

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
	/// 最後に成功した初期化・更新で確定した主人公の状態を取得する
	/// </summary>
	/// <returns>再生中の主人公の値。停止中、または主人公なしの場合はnullopt</returns>
	std::optional<CharacterRuntimeState> GetCharacterState() const;

	/// <summary>
	/// Rendererから参照できる、アドレスが変わらない再生用シーンを取得する
	/// </summary>
	/// <returns>編集データとは独立したシーン</returns>
	const Scene &GetScene() const {
		return scene_;
	}

	/// <summary>
	/// 現在の描画に使用するCamera物体のIDを取得する
	/// </summary>
	/// <returns>再生開始時に選ばれたCameraのID。停止中または固定ビューの場合は0</returns>
	SceneObjectId GetCameraObjectId() const {
		return playing_ ? camera_ : 0;
	}

	/// <summary>
	/// Primary Camera、または開始時に指定した固定カメラの姿勢を取得する
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

	/// <summary>
	/// 開始時または最後の正の時間の更新で生成したTriggerイベントを取得する。一時停止相当の0秒更新では保持する
	/// </summary>
	/// <returns>組のID順のイベント。次の正の時間の更新成功・再生開始・停止まで有効</returns>
	std::span<const TriggerEvent> GetTriggerEvents() const {
		return triggers_.GetEvents();
	}

	/// <summary>
	/// 再生シーンで現在重なるTriggerの組を取得する
	/// </summary>
	/// <returns>少なくとも一方がTriggerである接触の一覧。停止中は空</returns>
	std::span<const TriggerContact> GetTriggerContacts() const {
		return triggers_.GetContacts();
	}

  private:
	Scene scene_;
	CollisionWorld world_;
	TriggerWorld triggers_;
	CharacterController controller_;
	SceneObjectId character_ = 0, camera_ = 0;
	std::vector<SceneObjectId> updateOrder_;
	// 追従の補間状態はワールド座標で保持し、シーンにはローカル変形を反映する。
	std::map<SceneObjectId, CameraPose> followPoses_;
	DirectX::XMFLOAT3 spawn_{};
	std::uint64_t respawnCount_ = 0;
	std::optional<DirectX::XMFLOAT3> lastRespawnPosition_;
	CameraPose pose_{};
	CameraSettings cameraSettings_{};
	bool playing_ = false;
};
