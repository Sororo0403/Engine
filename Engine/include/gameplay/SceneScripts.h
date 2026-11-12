#pragma once
class SceneAnimations;
class SceneParticles;
class SceneAnimators;

#include "gameplay/GameActionState.h"
#include <array>
#include <span>

#include "gameplay/ScenePrefabs.h"
#include "gameplay/ScriptComponent.h"
#include "physics/CollisionEvent.h"
#include "physics/TriggerEvent.h"
#include "scripting/LuaScript.h"
#include "scripting/ScriptMailbox.h"
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
class Scene;
struct UIInput;
struct CharacterRuntimeState;
struct CameraTransitionView;
struct CharacterInput;

/// <summary>
/// コンパイル済みコードを共有し、物体ごとの状態・開始済み情報・次回へ送る接触を保持する
/// </summary>
class SceneScripts {
  public:
	static constexpr std::size_t kMaximumInstances = 256;
	static constexpr std::size_t kMaximumCallbacks = 1024;
	static constexpr std::size_t kMaximumLogs = 128;
	static constexpr int kFrameInstructions = 2000000;
	static constexpr int kFramePhysicsOperations = 1024;
	static constexpr int kFrameInstantiations = 32;
	static constexpr int kFrameDestructions = 256;
	static constexpr int kFrameAudioOperations = 1024;
	static constexpr int kFrameSceneRequests = 64;
	static constexpr std::size_t kFrameSpawnedObjects = 1024;
	static constexpr float kMaximumFrameSeconds = 0.1f;

	/// <summary>
	/// 有効なスクリプトを読み込み、必要なインスタンス状態を準備する。継続時は状態と読み込み済みコードを保持する
	/// </summary>
	/// <param name="scene">再生用の候補</param>
	/// <param name="preserve">true:既存の同じ設定の状態を保持 false:最初から準備する</param>
	/// <param name="error">失敗理由</param>
	/// <param name="invalidObject">指定時は失敗した物体ID</param>
	/// <returns>true:準備成功 false:コード・設定・件数が無効。以前の実行状態を保持する</returns>
	bool Configure(const Scene &scene, bool preserve, std::string &error,
	               SceneObjectId *invalidObject);

	/// <summary>
	/// 新規インスタンスのstart、update、接触コールバックを順に呼ぶ。変更は呼び出し側の候補へ限定する
	/// </summary>
	/// <param name="scene">作業用の候補シーン</param>
	/// <param name="seconds">0以上のゲーム時間。0秒ではupdateと接触を実行しない</param>
	/// <param name="input">共通ゲーム入力</param>
	/// <param name="events">今回の確定済み接触</param>
	/// <param name="startOnly">true:startだけ実行する</param>
	/// <param name="error">失敗理由</param>
	/// <param name="invalidObject">指定時は失敗した物体ID</param>
	/// <param name="uiClicks">今回クリックされたButtonのID。ゲーム内一時停止中もclickを通知する</param>
	/// <param name="physics">指定時はSceneと同じトランザクションの物理候補。失敗時は呼び出し側で破棄する</param>
	/// <param name="collisions">今回の通常接触。正の時間でTriggerの後、clickの前に両物体へ通知する</param>
	/// <param name="audioActions">指定時は成功した全コールバックの音声要求を末尾へ追加する。削除した音源への要求は取り除く</param>
	/// <param name="sceneRequest">指定時は成功したコールバックから最後の遷移先を返す</param>
	/// <param name="sharedState">指定時は全物体・コールバックで共有するgameのJSON。成功後に呼び出し側で確定する</param>
	/// <param name="actions">今回の名前付き入力。更新時だけLuaのinput.actionsへ渡す</param>
	/// <param name="animations">指定時は同じ候補のアニメーション再生状態。成功後だけ呼び出し側で確定する</param>
	/// <param name="particles">指定時は同じ候補の粒子再生状態。成功後だけ呼び出し側で確定する</param>
	/// <param name="animators">指定時は同じ候補のモデル内クリップの再生状態</param>
	/// <param name="viewport">指定時はLuaのupdateへ描画寸法と利用可能なポインターを渡す</param>
	/// <param name="transitionView">切り替え中のゲームカメラの描画姿勢。通常はnullptr</param>
	/// <param name="character">指定時はLuaのupdateへ主人公の更新済み状態を渡す</param>
	/// <param name="clipPauses">指定時はAnimation Playerの一時停止・再開要求を受け取る</param>
	/// <param name="characterAirflow">指定時は主人公への風速要求を受け取る。呼び出し元が更新ごとにゼロ初期化する</param>
	/// <returns>true:実行成功 false:コールバック・状態・制限が無効。呼び出し側は候補を破棄する</returns>
	bool Run(Scene &scene, float seconds, const CharacterInput &input,
	         std::span<const TriggerEvent> events, bool startOnly,
	         std::string &error, SceneObjectId *invalidObject,
	         std::span<const SceneObjectId> uiClicks = {},
	         ScenePhysics *physics = nullptr,
	         std::span<const CollisionEvent> collisions = {},
	         std::vector<SceneAudioAction> *audioActions = nullptr,
	         std::filesystem::path *sceneRequest = nullptr,
	         std::string *sharedState = nullptr,
	         std::span<const GameActionState> actions = {},
	         SceneAnimations *animations = nullptr,
	         SceneParticles *particles = nullptr,
	         SceneAnimators *animators = nullptr,
	         const UIInput *viewport = nullptr,
	         const CharacterRuntimeState *character = nullptr,
	         const CameraTransitionView *transitionView = nullptr,
	         std::map<SceneObjectId, bool> *clipPauses = nullptr,
	         std::array<float, 3> *characterAirflow = nullptr);

	/// <summary>
	/// スクリプトが生成した接触を再帰実行せず、次の正の時間の更新へ送る
	/// </summary>
	/// <param name="events">確定した接触。Stayは予約しない</param>
	void QueueTriggers(std::span<const TriggerEvent> events);

	/// <summary>
	/// Triggerから有効なLua Scriptに空のデータを持つ通知を予約する
	/// </summary>
	/// <param name="scene">操作を反映した再生シーン</param>
	/// <param name="receiver">通知先</param>
	/// <param name="sender">Triggerを持つ物体</param>
	/// <param name="name">通知名</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:追加成功または無効な受信先をスキップ false:キューの制限または通知名が無効</returns>
	bool QueueTriggerMessage(const Scene &scene, SceneObjectId receiver,
	                         SceneObjectId sender, std::string_view name,
	                         std::string &error);

	/// <summary>
	/// インスタンスの状態を、値を書き換えられないJSONとして参照する
	/// </summary>
	/// <param name="id">物体ID</param>
	/// <returns>初期化済みの状態。未初期化はnullopt。次の更新まで有効</returns>
	std::optional<std::string_view> GetState(SceneObjectId id) const;

	/// <summary>
	/// 全処理の成功後に、予約したConsoleメッセージを一度だけ出力する
	/// </summary>
	void FlushLogs();

	/// <summary>
	/// 処理対象のインスタンスまたは通知を保持するか確認する
	/// </summary>
	/// <returns>true:保持中 false:対象なし</returns>
	bool HasInstances() const {
		return !instances_.empty() || mailbox_.GetPendingCount() != 0;
	}

	/// <summary>
	/// 素材準備で参照する、読み込み済みPrefabを取得する
	/// </summary>
	/// <returns>次の設定変更まで有効な不変の階層一覧</returns>
	std::span<const std::shared_ptr<const Scene>> GetPrefabScenes() const {
		return prefabs_.GetScenes();
	}

	/// <summary>
	/// 次の更新を待つ通知の件数を取得する
	/// </summary>
	/// <returns>送信成功後、まだ受信処理していない件数</returns>
	std::size_t GetPendingMessageCount() const {
		return mailbox_.GetPendingCount();
	}

  private:
	struct Instance {
		ScriptComponent settings;
		LuaScriptProgram program;
		std::string state;
		bool started = false;
	};

	ScenePrefabs prefabs_;
	ScriptMailbox mailbox_;
	std::map<SceneObjectId, Instance> instances_;
	std::map<std::filesystem::path, LuaScriptProgram> programs_;
	std::vector<TriggerEvent> pending_;
	std::vector<std::pair<SceneObjectId, std::string>> logs_;
};
