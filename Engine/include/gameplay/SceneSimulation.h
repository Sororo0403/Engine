#pragma once
#include "animation/SceneAnimations.h"

#include "gameplay/GameActionState.h"
#include <span>

#include "gameplay/CharacterController.h"
#include "gameplay/CharacterRuntimeState.h"
#include "gameplay/FollowCameraOrbit.h"
#include "gameplay/SceneMotion.h"
#include "gameplay/ScenePhysics.h"
#include "gameplay/SceneScripts.h"
#include "graphics/camera/CameraPose.h"
#include "physics/CollisionEvents.h"
#include "physics/CollisionWorld.h"
#include "physics/TriggerWorld.h"
#include "scene/Scene.h"
#include "scene/SceneActiveChange.h"
#include "scene/SceneAudioActions.h"
#include "ui/SceneUI.h"
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

/// <summary>
/// 編集シーンから独立した再生用シーンで、移動・追従・復帰を実行する
/// </summary>
class SceneSimulation {

  public:
	/// <summary>
	/// 物体の再生位置と再生指定を取得する。編集用の設定には書き戻さない
	/// </summary>
	/// <param name="id">再生シーンの物体</param>
	/// <returns>再生状態。コンポーネントなし・停止中のシミュレーションはnullopt</returns>
	std::optional<AnimationPlaybackState>
	GetAnimationState(SceneObjectId id) const {
		return animations_.GetState(id);
	}

	/// <summary>
	/// 次の正の時間の更新を待つLua通知の件数を取得する
	/// </summary>
	/// <returns>停止・シーン遷移・Restartで0に戻る件数</returns>

	std::size_t GetPendingScriptMessages() const {
		return scripts_.GetPendingMessageCount();
	}

	/// <summary>
	/// ゲームの開始処理を実行せず、シーンの初期配置・物理・UIとLuaのコンパイルを検証する
	/// </summary>
	/// <param name="source">変更しない検証元</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <param name="fallbackPose">Cameraがない場合の姿勢</param>
	/// <param name="invalidObject">指定時は問題の物体ID</param>
	/// <returns>true:準備可能 false:設定・配置・コンパイルが無効</returns>
	/// <remarks>遷移先のstartが前のシーンの共有データを必要とする場合も、初期化を先行実行しない</remarks>
	static bool Validate(const Scene &source, std::string &error,
	                     const CameraPose &fallbackPose = CameraPose{},
	                     SceneObjectId *invalidObject = nullptr);

	/// <summary>
	/// 全物体とシーン遷移で共有するLuaのgameを、変更しないJSONとして取得する
	/// </summary>
	/// <returns>次の更新まで有効なJSON。Start・Restart・Stopで空に戻る</returns>
	std::string_view GetSharedScriptData() const {
		return sharedScriptData_;
	}

	/// <summary>
	/// 再生用の根と子孫を削除し、参照・物理・UI・スクリプト状態を同期する
	/// </summary>
	/// <param name="roots">存在する根のID。重複と親子の同時指定を許可する</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:成功 false:停止中・指定なし・不正な候補。失敗時は全状態を保持する</returns>
	/// <remarks>編集シーンとSceneRevisionは変更しない。GPU完了後に呼び、描画・音声は所有側の次の更新で同期する</remarks>
	bool DestroyObjects(std::span<const SceneObjectId> roots,
	                    std::string &error);

	/// <summary>
	/// 宣言されたPrefab素材を、候補の確定前に準備する処理
	/// </summary>
	using PreparePrefabResources = std::function<bool(
	    std::span<const std::shared_ptr<const Scene>>, std::string &)>;

	/// <summary>
	/// 描画・音声を所有する層から素材準備を登録する。停止しても登録を保持する
	/// </summary>
	/// <param name="prepare">GPU完了後に呼ぶ準備処理。空の場合はCPUの検証だけを行う</param>
	void SetPrefabResourcePreparation(PreparePrefabResources prepare) {
		preparePrefabResources_ = std::move(prepare);
	}

	/// <summary>
	/// 再生中のシーンへ独立したPrefab階層を追加し、物理・追従・UI・スクリプトの準備を同期する
	/// </summary>
	/// <param name="prefab">PrefabSerializerで読み込んだ単独階層。内容は変更しない</param>
	/// <param name="position">根の有限なワールド配置先</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>追加した根のID。停止中・設定や素材が無効な場合は0で全実行状態を保持する</returns>
	/// <remarks>編集シーンとSceneRevisionは変更しない。追加したスクリプトのstartは次の正の時間の更新で一度だけ呼ぶ。描画・音声の素材確保は呼び出し側が行う</remarks>
	SceneObjectId InstantiatePrefab(const Scene &prefab,
	                                const DirectX::XMFLOAT3 &position,
	                                std::string &error);

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
	/// 再生中の有効設定をまとめて変更し、描画用シーン・衝突・Trigger・Cameraの選択を同期する
	/// </summary>
	/// <param name="changes">重複のない既存IDとローカル有効設定。空は無効</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <param name="invalidObject">指定時は問題のID。成功時や対象に紐付かない失敗では0</param>
	/// <returns>true:変更成功または同値 false:未再生、無効な対象・配置・コンポーネント。失敗時は全状態を保持する</returns>
	/// <remarks>継続する主人公の速度・入力・復帰情報と追従の補間状態を保持する。新しく有効になった主人公は現在位置で初期化する。無効な枝のローカル姿勢と子自身の設定は保持する</remarks>
	bool SetActive(std::span<const SceneActiveChange> changes,
	               std::string &error, SceneObjectId *invalidObject = nullptr);

	/// <summary>
	/// 1物体のローカル有効設定を変更する
	/// </summary>
	/// <param name="id">既存の物体ID</param>
	/// <param name="active">変更後の設定</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <param name="invalidObject">指定時は問題のID</param>
	/// <returns>true:成功または同値 false:未再生、対象や変更後の状態が無効。失敗時は全状態を保持する</returns>
	bool SetActive(SceneObjectId id, bool active, std::string &error,
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
	/// <param name="cameraInput">現在のPrimary Follow Cameraの旋回入力。0秒では反映しない</param>
	/// <param name="uiInput">ゲーム内UIの操作。0秒では押下を解除し、クリックしない</param>
	/// <param name="actions">今回の名前付き入力。更新時だけLuaのinput.actionsへ渡す</param>
	/// <returns>true:更新成功 false:未再生または更新失敗</returns>
	bool Update(float seconds, const CharacterInput &input,
	            const OrbitCameraInput &cameraInput = {},
	            const UIInput &uiInput = {},
	            std::span<const GameActionState> actions = {});

	/// <summary>
	/// 開始・再開始・シーン遷移のたびに増える識別値を取得する。失敗では変えない
	/// </summary>
	/// <returns>物体IDの所属を識別する単調増加の値。未開始は0</returns>
	std::uint64_t GetSceneRevision() const {
		return sceneRevision_;
	}

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
	/// 有効なRigid Bodyの現在のワールド姿勢・速度・休止状態を取得する
	/// </summary>
	/// <param name="id">再生用シーンの物体ID</param>
	/// <returns>実行中の剛体状態。対象なし・無効・停止中はnullopt</returns>
	std::optional<RigidBodyState> GetRigidBodyState(SceneObjectId id) const;

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
	/// <returns>現在有効なPrimary CameraのID。停止中または固定ビューの場合は0</returns>
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
	/// 最後の正の時間の更新で生成した通常の接触通知を取得する。開始直後は空
	/// </summary>
	/// <returns>次の更新成功・再開始・停止まで有効。0秒更新と失敗では保持する</returns>
	std::span<const CollisionEvent> GetCollisionEvents() const {
		return collisions_.GetEvents();
	}

	/// <summary>
	/// 開始時・有効設定の変更時・最後の正の時間の更新で生成したTriggerイベントを取得する。一時停止相当の0秒更新では保持する
	/// </summary>
	/// <returns>検知した組のID順のイベントと、その操作で生成したイベント。次の正の時間の更新成功・有効設定の変更成功・再生開始・停止まで有効</returns>
	std::span<const TriggerEvent> GetTriggerEvents() const {
		return frameEvents_;
	}

	/// <summary>
	/// 成功した開始・正の時間の更新で確定した音声要求を取得する。0秒更新・一時停止・失敗では保持する
	/// </summary>
	/// <returns>次の成功または停止まで有効な参照。同じrevisionは再実行しない</returns>
	SceneAudioActions GetAudioActions() const {
		return {audioRevision_, audioActions_};
	}

	/// <summary>
	/// 最後のUpdateに失敗した理由を取得する。開始と更新の成功時は空になる
	/// </summary>
	/// <returns>次のUpdateまたは再生開始まで有効な失敗理由</returns>
	const std::string &GetLastUpdateError() const {
		return lastUpdateError_;
	}

	/// <summary>
	/// 再生シーンで現在重なるTriggerの組を取得する
	/// </summary>
	/// <returns>少なくとも一方がTriggerである接触の一覧。停止中は空</returns>
	std::span<const TriggerContact> GetTriggerContacts() const {
		return triggers_.GetContacts();
	}

	/// <summary>
	/// 初期化済みLuaインスタンスの状態を、変更しないJSONとして参照する
	/// </summary>
	/// <param name="id">物体ID</param>
	/// <returns>状態。未初期化はnullopt。次の更新まで有効</returns>
	std::optional<std::string_view> GetScriptState(SceneObjectId id) const {
		return scripts_.GetState(id);
	}

	/// <summary>
	/// 一時停止・フォーカス喪失時に、ゲーム内UIの操作を解除する
	/// </summary>
	void CancelUIInput() {
		ui_.Cancel();
	}

	/// <summary>
	/// 現在のボタン表示と入力消費の状態を取得する
	/// </summary>
	/// <returns>次の更新・停止まで有効な読み取り専用の状態</returns>
	const SceneUI &GetUI() const {
		return ui_;
	}

  private:
	/// <summary>
	/// シーンを開始し、遷移の場合だけ共有データを引き継ぐ
	/// </summary>
	/// <param name="source">変更しない開始元</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <param name="fallbackPose">Cameraがない場合の姿勢</param>
	/// <param name="invalidObject">指定時は問題の物体ID</param>
	/// <param name="preserveSharedData">true:シーン遷移 false:新しいPlayを開始する</param>
	/// <returns>true:開始成功 false:失敗。全実行状態を保持する</returns>
	bool StartScene(const Scene &source, std::string &error,
	                const CameraPose &fallbackPose,
	                SceneObjectId *invalidObject, bool preserveSharedData);

	/// <summary>
	/// 次の正の時間の更新で操作を受け取るTrigger Actionsと、その原因イベント
	/// </summary>
	struct ActionSignal {
		SceneObjectId receiver = 0;
		TriggerAction::Event event = TriggerAction::Event::ENTER;
	};

	/// <summary>
	/// 作業用シミュレーションへ、シーンと継続中の実行状態を複製する
	/// </summary>
	/// <param name="source">複製元</param>
	void CopyFrom(const SceneSimulation &source);

	/// <summary>
	/// 検証済みの作業結果を、公開シーン本体のアドレスを保って確定する
	/// </summary>
	/// <param name="source">確定する作業結果</param>
	void Adopt(SceneSimulation &source);

	/// <summary>
	/// 操作の処理前に、物理・追従・復帰とTriggerの更新を行う
	/// </summary>
	/// <param name="seconds">有効な経過時間</param>
	/// <param name="input">移動とジャンプ</param>
	/// <param name="cameraInput">Primary Cameraの旋回入力</param>
	/// <returns>true:更新成功 false:更新失敗</returns>
	bool Advance(float seconds, const CharacterInput &input,
	             const OrbitCameraInput &cameraInput);

	/// <summary>
	/// 進入・退出を、発生時に有効なTrigger Actionsの受信先へ振り分ける
	/// </summary>
	/// <param name="events">振り分けるイベント</param>
	/// <param name="signals">末尾へ追加する受信先とイベント</param>
	void QueueActionSignals(std::span<const TriggerEvent> events,
	                        std::vector<ActionSignal> &signals) const;

	/// <summary>
	/// 予約済みの検知と今回の検知から操作を集め、有効設定を一括変更してから音声要求を確定する
	/// </summary>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <param name="invalidObject">指定時は問題のIDを設定する</param>
	/// <param name="interactPressed">今回の操作入力。現在の主人公が重なるTriggerへ進入・退出の後に送る</param>
	/// <returns>true:成功 false:設定や適用先の状態が無効</returns>
	bool ApplyTriggerActions(std::string &error,
	                         SceneObjectId *invalidObject = nullptr,
	                         bool interactPressed = false);

	/// <summary>
	/// 開始または有効設定の変更を、作業シーンで検証してから確定する
	/// </summary>
	/// <param name="source">反映する設定</param>
	/// <param name="error">失敗理由</param>
	/// <param name="fallbackPose">Primaryがない場合の固定姿勢</param>
	/// <param name="invalidObject">問題のIDの出力先</param>
	/// <param name="preserve">継続するコンポーネントの実行状態を引き継ぐか</param>
	/// <returns>true:確定成功 false:検証失敗。現在のシーンと実行状態は保持する</returns>
	bool Configure(const Scene &source, std::string &error,
	               const CameraPose &fallbackPose, SceneObjectId *invalidObject,
	               bool preserve);

	/// <summary>
	/// Trigger操作の瞬間移動をまとめて検証し、衝突・追従・接触を同期する
	/// </summary>
	/// <param name="positions">移動対象と移動後のワールド位置</param>
	/// <param name="error">失敗理由</param>
	/// <param name="invalidObject">問題のIDの出力先</param>
	/// <returns>true:確定成功 false:配置や実行状態が無効。失敗時は全状態を保持する</returns>
	bool
	ApplyTeleports(const std::map<SceneObjectId, DirectX::XMFLOAT3> &positions,
	               std::string &error, SceneObjectId *invalidObject);

	/// <summary>
	/// スクリプトの状態と候補の変更を実行し、物理・追従・接触をまとめて検証する
	/// </summary>
	/// <param name="seconds">ゲーム時間</param>
	/// <param name="input">共通ゲーム入力</param>
	/// <param name="startOnly">true:startだけ実行する</param>
	/// <param name="error">失敗理由</param>
	/// <param name="invalidObject">問題の物体ID</param>
	/// <param name="actions">今回の名前付き入力。更新時だけLuaのinput.actionsへ渡す</param>
	/// <returns>true:成功 false:スクリプトまたは変更後の配置が無効</returns>
	bool ApplyScripts(float seconds, const CharacterInput &input,
	                  bool startOnly, std::string &error,
	                  SceneObjectId *invalidObject,
	                  std::span<const GameActionState> actions = {});

	/// <summary>
	/// 物体変更後の最終状態を検証し、瞬間的に動かした枝の速度・追従・接触を同期する
	/// </summary>
	/// <param name="candidate">全変更を含む候補</param>
	/// <param name="transformed">明示的に変形した物体</param>
	/// <param name="error">失敗理由</param>
	/// <param name="invalidObject">問題の物体ID</param>
	/// <param name="generated">指定時は今回のEnter/Exitを返す</param>
	/// <returns>true:確定成功 false:階層・物理・カメラ・コンポーネントが無効。失敗時は現在の状態を保持する</returns>
	bool
	SynchronizeSceneChanges(const Scene &candidate,
	                        std::span<const SceneObjectId> transformed,
	                        std::string &error, SceneObjectId *invalidObject,
	                        std::vector<TriggerEvent> *generated = nullptr);

	Scene scene_;
	std::string sharedScriptData_ = "{}";
	CollisionWorld world_;
	TriggerWorld triggers_;
	CollisionEvents collisions_;
	CharacterController controller_;
	SceneMotion motion_;
	SceneAnimations animations_;
	ScenePhysics physics_;
	SceneScripts scripts_;
	SceneUI ui_;
	SceneObjectId character_ = 0, camera_ = 0;
	std::vector<SceneObjectId> updateOrder_;
	// 追従の補間状態はワールド座標で保持し、シーンにはローカル変形を反映する。
	std::map<SceneObjectId, CameraPose> followPoses_;
	std::map<SceneObjectId, FollowCameraOrbit> orbitStates_;
	DirectX::XMFLOAT3 spawn_{};
	std::uint64_t respawnCount_ = 0;
	std::optional<DirectX::XMFLOAT3> lastRespawnPosition_;
	CameraPose pose_{};
	CameraPose fallbackPose_{};
	CameraSettings cameraSettings_{};
	PreparePrefabResources preparePrefabResources_;
	bool playing_ = false;
	std::vector<TriggerEvent> frameEvents_;
	std::vector<SceneAudioAction> audioActions_;
	std::uint64_t audioRevision_ = 0;
	std::vector<ActionSignal> pendingActionSignals_;
	std::set<std::pair<SceneObjectId, std::size_t>> firedActions_;
	std::string lastUpdateError_;
	std::filesystem::path pendingScene_;
	std::uint64_t sceneRevision_ = 0;
};
