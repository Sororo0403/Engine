#pragma once

#include "gameplay/GameActionBinding.h"
#include "platform/windows/PathUtils.h"
#include "scene/ObjectTag.h"
#include "scene/SceneObjectId.h"
#include "scripting/ScriptMessage.h"
#include <cmath>
#include <filesystem>
#include <string>

/// <summary>
/// Triggerの進入・退出や範囲内での操作入力、UI Buttonのクリックに応じて、対象物体の有効設定・音声・瞬間移動・復帰先やカメラの変更、またはシーン遷移を要求する
/// </summary>
struct TriggerAction {
	static constexpr float kMaximumDelaySeconds = 600.f;
	static constexpr float kMaximumCameraBlendSeconds = 30.f;
	/// <summary>
	/// 対象へ適用する有効設定、Audio Sourceの再生・停止、瞬間移動・復帰先やカメラの変更、またはシーンの読み込み
	/// </summary>
	enum class Operation {
		ENABLE,
		DISABLE,
		TOGGLE,
		PLAY_AUDIO,
		STOP_AUDIO,
		TELEPORT,
		SET_CHECKPOINT,
		SET_CAMERA,
		PLAY_MOTION_PATH,
		REVERSE_MOTION_PATH,
		PAUSE_MOTION_PATH,
		PLAY_TRANSFORM_ANIMATION,
		RESTART_TRANSFORM_ANIMATION,
		PAUSE_TRANSFORM_ANIMATION,
		STOP_TRANSFORM_ANIMATION,
		EMIT_PARTICLES,
		PLAY_ANIMATOR,
		RESTART_ANIMATOR,
		PAUSE_ANIMATOR,
		STOP_ANIMATOR,
		SEND_SCRIPT_MESSAGE,
		LOAD_SCENE
	};

	/// <summary>
	/// 物理的な接触、範囲内での操作入力、またはUI Buttonのクリックを指定する実行条件
	/// </summary>
	enum class Event {
		ENTER,
		EXIT,
		INTERACT,
		FIRST_ENTER,
		LAST_EXIT,
		BUTTON_CLICK
	};

	/// <summary>
	/// 操作を発火させる接触相手のコンポーネント条件
	/// </summary>
	enum class Contact { ANY, CHARACTER, DYNAMIC_BODY };

	Event event = Event::ENTER;
	SceneObjectId target = 0;
	Operation operation = Operation::ENABLE;
	bool once = false;
	// Teleportの移動先、またはSet Checkpointの復帰先。物体のワールド位置を使用する。
	SceneObjectId destination = 0;
	// Load Sceneの遷移先。空は編集中だけ許可する。絶対パスまたは実行ファイル基準
	std::filesystem::path scenePath;
	Contact contact = Contact::ANY;
	// 接触相手のタグ。空の場合はタグによる絞り込みを行わない。
	std::string contactTag;
	// Interactで使う名前付き操作。空の場合は既定のInteractを使う。
	std::string inputAction;
	// 受信側のLuaに渡す通知名。空は編集中だけ許可する。
	std::string messageName;
	// 条件を満たしてから操作を実行するまでのシミュレーション秒数。
	float delaySeconds = 0;
	// Set Cameraで描画姿勢を切り替える時間。0で即時に切り替える。
	float cameraBlendSeconds = 0;
	// Load Sceneで主人公の到着位置を指定する遷移先のタグ。空は配置済みの位置を使う。
	std::string arrivalTag;

	/// <summary>
	/// 種別が有効か確認する。未割り当ての対象は編集用に許可し、Play時に存在を確認する
	/// </summary>
	/// <returns>true:有効 false:未対応のイベントまたは操作</returns>
	bool IsValid() const {
		return std::isfinite(delaySeconds) && delaySeconds >= 0 &&
		       delaySeconds <= kMaximumDelaySeconds &&
		       std::isfinite(cameraBlendSeconds) && cameraBlendSeconds >= 0 &&
		       cameraBlendSeconds <= kMaximumCameraBlendSeconds &&
		       (cameraBlendSeconds == 0 ||
		        operation == Operation::SET_CAMERA) &&
		       ObjectTag::IsValid(contactTag) &&
		       (arrivalTag.empty() || (operation == Operation::LOAD_SCENE &&
		                               ObjectTag::IsValid(arrivalTag))) &&
		       (messageName.empty() ||
		        (operation == Operation::SEND_SCRIPT_MESSAGE &&
		         ScriptMessage::IsValidName(messageName))) &&
		       (inputAction.empty() ||
		        (event == Event::INTERACT &&
		         GameActionBinding::IsValidName(inputAction))) &&
		       (scenePath.empty() ||
		        (scenePath.native().find(L'\0') == std::wstring::npos &&
		         (!scenePath.has_root_path() || scenePath.is_absolute()) &&
		         PathUtils::HasExtension(scenePath, ".json") &&
		         PathUtils::HasExtension(scenePath.stem(), ".scene"))) &&
		       (contact == Contact::ANY || contact == Contact::CHARACTER ||
		        contact == Contact::DYNAMIC_BODY) &&
		       (event == Event::ENTER || event == Event::EXIT ||
		        event == Event::INTERACT || event == Event::FIRST_ENTER ||
		        event == Event::LAST_EXIT || event == Event::BUTTON_CLICK) &&
		       (event != Event::BUTTON_CLICK ||
		        (contact == Contact::ANY && contactTag.empty())) &&
		       (contact != Contact::DYNAMIC_BODY || AllowsDynamicContact()) &&
		       (operation == Operation::ENABLE ||
		        operation == Operation::DISABLE ||
		        operation == Operation::TOGGLE || IsAudioOperation() ||
		        UsesDestination() || operation == Operation::SET_CAMERA ||
		        IsMotionPathOperation() || IsTransformAnimationOperation() ||
		        IsAnimatorOperation() ||
		        operation == Operation::EMIT_PARTICLES ||
		        operation == Operation::SEND_SCRIPT_MESSAGE ||
		        operation == Operation::LOAD_SCENE);
	}

	/// <summary>
	/// 接触相手ごとの通過でなく、該当する物体がいる状態の変化で発火するか確認する
	/// </summary>
	/// <returns>true:最初の進入または最後の退出 false:個別の接触や操作入力</returns>
	bool UsesOccupancy() const {
		return event == Event::FIRST_ENTER || event == Event::LAST_EXIT;
	}

	/// <summary>
	/// 接触相手として動的剛体を指定できる操作か確認する
	/// </summary>
	/// <returns>true:動的剛体で発火可能 false:主人公の操作または復帰先設定</returns>
	bool AllowsDynamicContact() const {
		return event != Event::INTERACT && event != Event::BUTTON_CLICK &&
		       operation != Operation::SET_CHECKPOINT;
	}

	/// <summary>
	/// シーン内の物体を移動先または復帰先として使う操作か確認する
	/// </summary>
	/// <returns>true:配置先を使う false:配置先は不要</returns>
	bool UsesDestination() const {
		return operation == Operation::TELEPORT ||
		       operation == Operation::SET_CHECKPOINT;
	}

	/// <summary>
	/// 対象のMotion Pathを順再生・逆再生・一時停止する操作か確認する
	/// </summary>
	/// <returns>true:経路の操作 false:その他の操作</returns>
	bool IsMotionPathOperation() const {
		return operation == Operation::PLAY_MOTION_PATH ||
		       operation == Operation::REVERSE_MOTION_PATH ||
		       operation == Operation::PAUSE_MOTION_PATH;
	}

	/// <summary>
	/// 対象のTransform Animationを操作する種別か確認する
	/// </summary>
	/// <returns>true:変形アニメーションの操作 false:その他の操作</returns>
	bool IsTransformAnimationOperation() const {
		return operation == Operation::PLAY_TRANSFORM_ANIMATION ||
		       operation == Operation::RESTART_TRANSFORM_ANIMATION ||
		       operation == Operation::PAUSE_TRANSFORM_ANIMATION ||
		       operation == Operation::STOP_TRANSFORM_ANIMATION;
	}

	/// <summary>
	/// 対象のAnimatorで現在のクリップの再生状態を操作する種別か確認する
	/// </summary>
	/// <returns>true:Animatorの操作 false:その他の操作</returns>
	bool IsAnimatorOperation() const {
		return operation == Operation::PLAY_ANIMATOR ||
		       operation == Operation::RESTART_ANIMATOR ||
		       operation == Operation::PAUSE_ANIMATOR ||
		       operation == Operation::STOP_ANIMATOR;
	}

	/// <summary>
	/// 対象にAudio Sourceが必要な操作か確認する
	/// </summary>
	/// <returns>true:音声操作 false:有効設定の操作または未対応の値</returns>
	bool IsAudioOperation() const {
		return operation == Operation::PLAY_AUDIO ||
		       operation == Operation::STOP_AUDIO;
	}

	/// <summary>
	/// 対象と実行条件が一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const TriggerAction &other) const = default;
};
