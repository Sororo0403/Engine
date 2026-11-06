#pragma once

#include "gameplay/ScriptComponent.h"
#include "physics/TriggerEvent.h"
#include "scripting/LuaScript.h"
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
class Scene;
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
	/// <param name="uiClicks">今回クリックされたButtonのID。正の時間でだけclickを通知する</param>
	/// <returns>true:実行成功 false:コールバック・状態・制限が無効。呼び出し側は候補を破棄する</returns>
	bool Run(Scene &scene, float seconds, const CharacterInput &input,
	         std::span<const TriggerEvent> events, bool startOnly,
	         std::string &error, SceneObjectId *invalidObject,
	         std::span<const SceneObjectId> uiClicks = {});

	/// <summary>
	/// スクリプトが生成した接触を再帰実行せず、次の正の時間の更新へ送る
	/// </summary>
	/// <param name="events">確定した接触。Stayは予約しない</param>
	void QueueTriggers(std::span<const TriggerEvent> events);

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
	/// 初期化済みインスタンスを保持するか確認する
	/// </summary>
	/// <returns>true:保持中 false:対象なし</returns>
	bool HasInstances() const {
		return !instances_.empty();
	}

  private:
	struct Instance {
		ScriptComponent settings;
		LuaScriptProgram program;
		std::string state;
		bool started = false;
	};

	std::map<SceneObjectId, Instance> instances_;
	std::map<std::filesystem::path, LuaScriptProgram> programs_;
	std::vector<TriggerEvent> pending_;
	std::vector<std::pair<SceneObjectId, std::string>> logs_;
};
