#pragma once

#include "gameplay/GameActionState.h"
#include <span>

#include "scene/SceneAudioAction.h"
#include "scene/SceneObjectId.h"
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
class Scene;
class ScenePhysics;
class ScenePrefabs;
class ScriptMailbox;
class SceneAnimations;
class SceneParticles;
struct ScriptMessage;
struct CharacterInput;

class LuaScript;

/// <summary>
/// コンパイル済みLuaを共有する、VMや外部ライブラリの型を公開しない不変のハンドル
/// </summary>
class LuaScriptProgram {
  public:
	/// <summary>
	/// コンパイル済みのコードを保持しているか確認する
	/// </summary>
	/// <returns>true:使用可能 false:未設定</returns>
	explicit operator bool() const {
		return bool(data_);
	}

  private:
	friend class LuaScript;
	struct Data;
	std::shared_ptr<const Data> data_;
};

/// <summary>
/// Luaを内部だけで使用し、制限したAPIと明示的なstateでコールバックを実行する
/// </summary>
class LuaScript {
  public:
	/// <summary>
	/// 公開するスクリプトAPIの版。Runtimeの出力互換性を照合する
	/// </summary>
	static constexpr int kApiVersion = 12;
	static constexpr std::size_t kMaximumSourceBytes = 256 * 1024;
	static constexpr std::size_t kMaximumBytecodeBytes = 1024 * 1024;
	static constexpr std::size_t kMaximumStateBytes = 64 * 1024;
	static constexpr std::size_t kMaximumStateValues = 4096;
	static constexpr int kMaximumStateDepth = 16;
	static constexpr std::size_t kMaximumVmBytes = 4 * 1024 * 1024;
	static constexpr int kMaximumInstructions = 200000;
	static constexpr int kMaximumPhysicsOperations = 128;
	static constexpr int kMaximumInstantiations = 16;
	static constexpr int kMaximumDestructions = 64;
	static constexpr int kMaximumAudioOperations = 128;
	static constexpr int kMaximumSceneRequests = 8;
	static constexpr int kMaximumMessages = 32;
	static constexpr int kHookInterval = 100;
	static constexpr std::size_t kMaximumLogBytes = 1024;

	/// <summary>
	/// サイズを制限してLuaのテキストを読み込む。先頭のUTF-8 BOMは取り除く
	/// </summary>
	/// <param name="path">絶対パスまたは実行ファイル基準の.luaファイル</param>
	/// <param name="source">成功時だけ置き換えるコード</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:読み込み成功 false:参照・サイズ・読み込みが無効</returns>
	static bool Read(const std::filesystem::path &path, std::string &source,
	                 std::string &error);

	/// <summary>
	/// コードを実行せず構文を検証する。バイナリのチャンクは受け付けない
	/// </summary>
	/// <param name="source">Luaのテキスト</param>
	/// <param name="error">失敗した行と理由</param>
	/// <returns>true:構文有効 false:構文・サイズ・メモリが無効</returns>
	static bool Validate(std::string_view source, std::string &error);

	/// <summary>
	/// テキストをコンパイルし、実行しない不変のプログラムへ変換する
	/// </summary>
	/// <param name="source">Luaテキスト</param>
	/// <param name="name">行番号とともにエラーへ表示するアセット名</param>
	/// <param name="program">成功時だけ置き換えるプログラム</param>
	/// <param name="error">構文やサイズの失敗理由</param>
	/// <returns>true:コンパイル成功 false:無効なコード。以前のプログラムを保持する</returns>
	static bool Compile(std::string_view source, std::string_view name,
	                    LuaScriptProgram &program, std::string &error);

	/// <summary>
	/// 開始状態を検証し、JSONの表記を揃える。null・循環・関数・物体参照はstateに保存しない
	/// </summary>
	/// <param name="source">JSONオブジェクト</param>
	/// <param name="normalized">成功時だけ置き換えるJSON</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:状態有効 false:型・深さ・件数・サイズが無効</returns>
	static bool NormalizeState(std::string_view source, std::string &normalized,
	                           std::string &error);

	/// <summary>
	/// 宣言用のチャンクから指定コールバックを呼び、成功時だけstateを返す。シーンは呼び出し側の候補を使用する
	/// </summary>
	/// <param name="program">コンパイル済みの不変なコード</param>
	/// <param name="callback">start・update・trigger_enter・trigger_stay・trigger_exit・collision_enter・collision_stay・collision_exit・click・message</param>
	/// <param name="scene">作業シーン。全コールバックと物理検証の成功後だけ呼び出し側で確定する</param>
	/// <param name="self">スクリプトを持つ物体</param>
	/// <param name="state">成功時だけ置き換えるインスタンス状態</param>
	/// <param name="seconds">ゲーム時間。startと接触では0も指定できる</param>
	/// <param name="input">共通のゲーム入力</param>
	/// <param name="other">接触相手。開始・更新は0</param>
	/// <param name="logs">成功したコールバックのメッセージを追加する。呼び出し側で確定後に出力する</param>
	/// <param name="error">ファイル名・行・失敗理由</param>
	/// <param name="remainingInstructions">指定時はフレーム全体で共有する残り命令数。失敗時も消費した命令数を減らす</param>
	/// <param name="physics">指定時は呼び出し側の候補となる物理状態。Sceneとともに全処理成功後だけ確定する</param>
	/// <param name="remainingPhysicsOperations">指定時はフレームで共有する残り物理API回数。失敗時も消費分を減らす</param>
	/// <param name="prefabs">指定時は準備済みの不変なPrefabライブラリ</param>
	/// <param name="remainingInstantiations">指定時はフレーム全体で共有する残り生成回数</param>
	/// <param name="remainingSpawnedObjects">指定時は生成する根と子孫の残り総数</param>
	/// <param name="pendingDestructions">指定時は全コールバック成功後に削除する根の候補を追加する。呼び出し側が一括反映する</param>
	/// <param name="remainingDestructions">指定時はフレーム全体で共有する残り削除要求回数</param>
	/// <param name="audioActions">指定時は候補へ追加する音声要求。全処理成功後に呼び出し側で確定する</param>
	/// <param name="remainingAudioOperations">指定時はフレームで共有する残り音声API回数</param>
	/// <param name="sceneRequest">指定時は候補の遷移先を最後の要求で置き換える。呼び出し側で全処理成功後に確定する</param>
	/// <param name="remainingSceneRequests">指定時はフレームで共有する残り遷移要求回数</param>
	/// <param name="sharedState">指定時はgameとして公開する共有JSON。成功時だけ置き換える。stateと同じ制限を使い、同じ文字列を両方へ渡さない</param>
	/// <param name="mailbox">指定時は作業用の次回通知キュー。呼び出し側で全処理成功後に確定する</param>
	/// <param name="message">messageコールバックへ渡す名前・JSON・送信者。受信者はselfと一致すること</param>
	/// <param name="actions">今回の名前付き入力。更新時だけLuaのinput.actionsへ渡す</param>
	/// <param name="animations">指定時は同じ候補のアニメーション再生状態</param>
	/// <param name="remainingAnimationOperations">指定時はフレームで共有する残り操作回数</param>
	/// <param name="particles">指定時は同じ候補の粒子再生状態</param>
	/// <param name="remainingParticleOperations">指定時はフレーム全体で共有する残り粒子操作回数</param>
	/// <returns>true:成功またはコールバックなし false:コード・API・状態・実行制限が無効</returns>
	static bool
	Invoke(const LuaScriptProgram &program, std::string_view callback,
	       Scene &scene, SceneObjectId self, std::string &state, float seconds,
	       const CharacterInput &input, SceneObjectId other,
	       std::vector<std::string> &logs, std::string &error,
	       int *remainingInstructions = nullptr,
	       ScenePhysics *physics = nullptr,
	       int *remainingPhysicsOperations = nullptr,
	       const ScenePrefabs *prefabs = nullptr,
	       int *remainingInstantiations = nullptr,
	       std::size_t *remainingSpawnedObjects = nullptr,
	       std::vector<SceneObjectId> *pendingDestructions = nullptr,
	       int *remainingDestructions = nullptr,
	       std::vector<SceneAudioAction> *audioActions = nullptr,
	       int *remainingAudioOperations = nullptr,
	       std::filesystem::path *sceneRequest = nullptr,
	       int *remainingSceneRequests = nullptr,
	       std::string *sharedState = nullptr, ScriptMailbox *mailbox = nullptr,
	       const ScriptMessage *message = nullptr,
	       std::span<const GameActionState> actions = {},
	       SceneAnimations *animations = nullptr,
	       int *remainingAnimationOperations = nullptr,
	       SceneParticles *particles = nullptr,
	       int *remainingParticleOperations = nullptr);
};
