#pragma once

#include "gameplay/PlaybackClock.h"
#include "gameplay/SceneSimulation.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

/// <summary>
/// 編集シーンの開始時データを保持し、再生・再実行・一時停止の状態を管理する
/// </summary>
class EditorPlaySession {
  public:
	/// <summary>
	/// 開始時のシーンを保持して再生する。失敗時は現在の再生状態を保持する
	/// </summary>
	/// <param name="scene">複製元の編集シーン。値は変更しない</param>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <param name="fallbackPose">Primary Cameraがない場合の開始姿勢</param>
	/// <param name="invalidObject">指定時は問題の物体IDを設定する。成功時や物体に紐付かない失敗では0</param>
	/// <param name="cameraOverride">0は通常のPrimary選択。指定時は再生複製内だけ、そのCameraをPrimaryとして使用する</param>
	/// <returns>true:開始成功 false:設定が無効、または複製失敗</returns>
	bool Start(const Scene &scene, std::string &error,
	           const CameraPose &fallbackPose = CameraPose{},
	           SceneObjectId *invalidObject = nullptr,
	           SceneObjectId cameraOverride = 0);

	/// <summary>
	/// Play開始時のシーンから再実行する。一時停止と再生速度は保持し、予約済みのStepは破棄する
	/// </summary>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <returns>true:再実行成功 false:未再生、または初期化失敗。失敗時は現在の状態を保持する</returns>
	bool Restart(std::string &error);

	/// <summary>
	/// 再生シーンと開始時データを破棄し、時間制御を初期化する
	/// </summary>
	void Stop();

	/// <summary>
	/// 再生中の一時停止と再開を切り替える
	/// </summary>
	void TogglePause();

	/// <summary>
	/// 一時停止中の1フレーム実行を予約する
	/// </summary>
	void RequestStep();

	/// <summary>
	/// 再生中の速度を変更する。ゲーム設定と、一時停止・Stepの状態は保持する
	/// </summary>
	/// <param name="scale">有限な0.1倍から1倍までの速度</param>
	/// <returns>true:設定成功 false:停止中、または値が無効</returns>
	bool SetTimeScale(float scale);

	/// <summary>
	/// 現在の再生速度を取得する。停止中は1倍を返す
	/// </summary>
	/// <returns>実時間に掛ける倍率</returns>
	float GetTimeScale() const;

	/// <summary>
	/// 再生シーンの更新に成功したゲーム時間を累積して取得する
	/// </summary>
	/// <returns>速度倍率とフレーム上限を反映した秒数。Start・Restart・Stopで0に戻る</returns>
	double GetElapsedSeconds() const;

	/// <summary>
	/// 正のゲーム時間で更新に成功した回数を取得する。描画回数や物理の内部刻み数は含まない
	/// </summary>
	/// <returns>再生フレーム数。Pause中・0秒の更新・開始直後の表示では増えず、Start・Restart・Stopで0に戻る</returns>
	std::uint64_t GetSimulationFrameCount() const;

	/// <summary>
	/// 時間制御に従って再生シーンを更新する。開始・再実行後の最初の表示は初期状態を保持する
	/// </summary>
	/// <param name="seconds">有限な0以上の経過秒数</param>
	/// <param name="input">今回の移動・ジャンプ入力</param>
	/// <returns>true:更新成功または一時停止 false:未再生、無効な時間、または更新失敗</returns>
	bool Update(float seconds, const CharacterInput &input);

	/// <summary>
	/// 再生状態を取得する
	/// </summary>
	/// <returns>true:再生中 false:停止中</returns>
	bool IsPlaying() const;

	/// <summary>
	/// 一時停止状態を取得する
	/// </summary>
	/// <returns>true:再生中かつ一時停止 false:通常再生または停止中</returns>
	bool IsPaused() const;

	/// <summary>
	/// 最後に確定した主人公の再生状態を取得する。一時停止中も値を保持する
	/// </summary>
	/// <returns>再生中の主人公の値。停止中、または主人公なしの場合はnullopt</returns>
	std::optional<CharacterRuntimeState> GetCharacterState() const;

	/// <summary>
	/// 描画用の再生シーンを取得する。再実行してもシーン本体のアドレスは変わらない
	/// </summary>
	/// <returns>編集シーンから独立した再生シーン</returns>
	const Scene &GetScene() const;

	/// <summary>
	/// 描画中のCamera物体を取得する
	/// </summary>
	/// <returns>再生開始時に選ばれたCameraのID。停止中または固定ビューの場合は0</returns>
	SceneObjectId GetCameraObjectId() const;

	/// <summary>
	/// 再生カメラの姿勢を取得する
	/// </summary>
	/// <returns>現在の描画姿勢</returns>
	const CameraPose &GetCameraPose() const;

	/// <summary>
	/// 再生カメラの投影設定を取得する
	/// </summary>
	/// <returns>現在の投影設定</returns>
	const CameraSettings &GetCameraSettings() const;

	/// <summary>
	/// Play中の進入・退出を、発生したフレームとゲーム時間とともに保持する
	/// </summary>
	struct TriggerRecord {
		TriggerEvent event;
		std::uint64_t frame = 0;
		double seconds = 0;
	};

	static constexpr std::size_t kMaximumTriggerRecords = 128;

	/// <summary>
	/// 開始時または最後の正のゲーム時間の更新によるTriggerイベントを取得する
	/// </summary>
	/// <returns>Enter・Stay・Exitの一覧。一時停止中は最後の値を保持し、停止時は空になる</returns>
	std::span<const TriggerEvent> GetTriggerEvents() const;

	/// <summary>
	/// 現在Triggerと重なっている組を取得する
	/// </summary>
	/// <returns>ID順の接触一覧。瞬間的な通過の組は含まない</returns>
	std::span<const TriggerContact> GetTriggerContacts() const;

	/// <summary>
	/// 最新128件までの進入・退出を、古い順で取得する。Stayは履歴へ追加しない
	/// </summary>
	/// <returns>Start・Restart・Stopで初期化する履歴。開始時のEnterはフレーム0、時間0</returns>
	std::span<const TriggerRecord> GetTriggerHistory() const;

  private:
	/// <summary>
	/// 最後の更新で確定した進入・退出を、再生時間とフレーム数とともに固定長の履歴へ記録する
	/// </summary>
	void RememberTriggerEvents();

	SceneSimulation simulation_;
	std::unique_ptr<Scene> initialScene_;
	CameraPose initialFallback_{};
	PlaybackClock clock_;
	double elapsedSeconds_ = 0.0;
	std::uint64_t simulationFrameCount_ = 0;
	bool firstFrame_ = false;
	std::array<TriggerRecord, kMaximumTriggerRecords> triggerRecords_{};
	std::size_t triggerRecordCount_ = 0;
};
