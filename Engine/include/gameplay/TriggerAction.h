#pragma once

#include "platform/windows/PathUtils.h"
#include "scene/SceneObjectId.h"
#include <filesystem>

/// <summary>
/// Triggerの進入・退出、または範囲内での操作入力に応じて、対象物体の有効設定・音声・瞬間移動、またはシーン遷移を要求する
/// </summary>
struct TriggerAction {
	/// <summary>
	/// 対象へ適用する有効設定、Audio Sourceの再生・停止、瞬間移動、またはシーンの読み込み
	/// </summary>
	enum class Operation {
		ENABLE,
		DISABLE,
		TOGGLE,
		PLAY_AUDIO,
		STOP_AUDIO,
		TELEPORT,
		LOAD_SCENE
	};

	/// <summary>
	/// 物理的な接触イベントと、範囲内で押した操作入力を区別する実行条件
	/// </summary>
	enum class Event { ENTER, EXIT, INTERACT };

	Event event = Event::ENTER;
	SceneObjectId target = 0;
	Operation operation = Operation::ENABLE;
	bool once = false;
	// Teleportの移動先。物体のワールド位置を使用し、回転と拡縮は維持する。
	SceneObjectId destination = 0;
	// Load Sceneの遷移先。空は編集中だけ許可する。絶対パスまたは実行ファイル基準
	std::filesystem::path scenePath;

	/// <summary>
	/// 種別が有効か確認する。未割り当ての対象は編集用に許可し、Play時に存在を確認する
	/// </summary>
	/// <returns>true:有効 false:未対応のイベントまたは操作</returns>
	bool IsValid() const {
		return (scenePath.empty() ||
		        (scenePath.native().find(L'\0') == std::wstring::npos &&
		         (!scenePath.has_root_path() || scenePath.is_absolute()) &&
		         PathUtils::HasExtension(scenePath, ".json") &&
		         PathUtils::HasExtension(scenePath.stem(), ".scene"))) &&
		       (event == Event::ENTER || event == Event::EXIT ||
		        event == Event::INTERACT) &&
		       (operation == Operation::ENABLE ||
		        operation == Operation::DISABLE ||
		        operation == Operation::TOGGLE || IsAudioOperation() ||
		        operation == Operation::TELEPORT ||
		        operation == Operation::LOAD_SCENE);
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
