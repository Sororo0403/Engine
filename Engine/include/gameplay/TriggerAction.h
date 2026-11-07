#pragma once

#include "platform/windows/PathUtils.h"
#include "scene/ObjectTag.h"
#include "scene/SceneObjectId.h"
#include <filesystem>
#include <string>

/// <summary>
/// Triggerの進入・退出、または範囲内での操作入力に応じて、対象物体の有効設定・音声・瞬間移動・復帰先やカメラの変更、またはシーン遷移を要求する
/// </summary>
struct TriggerAction {
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
		PAUSE_MOTION_PATH,
		LOAD_SCENE
	};

	/// <summary>
	/// 物理的な接触イベントと、範囲内で押した操作入力を区別する実行条件
	/// </summary>
	enum class Event { ENTER, EXIT, INTERACT };

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

	/// <summary>
	/// 種別が有効か確認する。未割り当ての対象は編集用に許可し、Play時に存在を確認する
	/// </summary>
	/// <returns>true:有効 false:未対応のイベントまたは操作</returns>
	bool IsValid() const {
		return ObjectTag::IsValid(contactTag) &&
		       (scenePath.empty() ||
		        (scenePath.native().find(L'\0') == std::wstring::npos &&
		         (!scenePath.has_root_path() || scenePath.is_absolute()) &&
		         PathUtils::HasExtension(scenePath, ".json") &&
		         PathUtils::HasExtension(scenePath.stem(), ".scene"))) &&
		       (contact == Contact::ANY || contact == Contact::CHARACTER ||
		        contact == Contact::DYNAMIC_BODY) &&
		       (event == Event::ENTER || event == Event::EXIT ||
		        event == Event::INTERACT) &&
		       (contact != Contact::DYNAMIC_BODY || AllowsDynamicContact()) &&
		       (operation == Operation::ENABLE ||
		        operation == Operation::DISABLE ||
		        operation == Operation::TOGGLE || IsAudioOperation() ||
		        UsesDestination() || operation == Operation::SET_CAMERA ||
		        IsMotionPathOperation() || operation == Operation::LOAD_SCENE);
	}

	/// <summary>
	/// 接触相手として動的剛体を指定できる操作か確認する
	/// </summary>
	/// <returns>true:動的剛体で発火可能 false:主人公の操作または復帰先設定</returns>
	bool AllowsDynamicContact() const {
		return event != Event::INTERACT &&
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
	/// 対象のMotion Pathを再生または一時停止する操作か確認する
	/// </summary>
	/// <returns>true:経路の操作 false:その他の操作</returns>
	bool IsMotionPathOperation() const {
		return operation == Operation::PLAY_MOTION_PATH ||
		       operation == Operation::PAUSE_MOTION_PATH;
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
