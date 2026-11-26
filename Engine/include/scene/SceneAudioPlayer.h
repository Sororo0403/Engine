#pragma once

#include "audio/AudioClipHandle.h"
#include "audio/AudioPlaybackHandle.h"
#include "audio/AudioSystem.h"
#include "scene/SceneAudio.h"
#include "scene/SceneAudioActions.h"
#include <map>
#include <optional>

class AudioClipManager;

/// <summary>
/// 1シーンの有効な音源だけを再生し、停止・一時停止・再開をほかの再生から独立して管理する
/// </summary>
class SceneAudioPlayer {
  public:
	/// <summary>
	/// アドレスが変わらない再生用シーンと、より長く生存する共通音声機能を参照する
	/// </summary>
	/// <param name="scene">再生用シーン。編集用のシーンを変更しない</param>
	/// <param name="clips">音声データの共有管理</param>
	/// <param name="audio">デバイスと独立した音声再生</param>
	SceneAudioPlayer(const Scene &scene, AudioClipManager &clips,
	                 AudioSystem &audio);
	~SceneAudioPlayer();
	SceneAudioPlayer(const SceneAudioPlayer &) = delete;
	SceneAudioPlayer &operator=(const SceneAudioPlayer &) = delete;

	/// <summary>
	/// 全音源の設定とWAVを確認してから、以前の所有再生を止めて開始する。デバイス不在なら無音で待つ
	/// </summary>
	/// <param name="camera">リスナー未指定時に使う現在の描画姿勢</param>
	/// <param name="paused">初めから再生位置を止めておくか</param>
	/// <param name="error">成功時は空、設定・読み込み・再生失敗時は理由</param>
	/// <param name="invalidObject">指定時は問題のID</param>
	/// <param name="actions">開始時に確定した要求。同じ音源では最後の操作を採用し、停止対象は自動再生もしない</param>
	/// <returns>true:開始成功 false:無効なシーン・音声またはデバイス処理失敗</returns>
	bool Start(const CameraPose &camera, bool paused, std::string &error,
	           SceneObjectId *invalidObject = nullptr,
	           const SceneAudioActions &actions = {});

	/// <summary>
	/// 成功したシミュレーションの有効状態・ワールド位置・一時停止状態を音へ反映する。音量だけの変更では再生位置と停止状態を保持する
	/// </summary>
	/// <param name="camera">現在の描画姿勢</param>
	/// <param name="paused">音声の再生位置を止めるか。1フレーム実行中もtrueを渡す</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <param name="invalidObject">指定時は問題のID</param>
	/// <param name="actions">シミュレーションが最後に確定した要求。同じrevisionは再実行せず、古いrevisionは拒否する。無効・未割り当ての音源は操作しない</param>
	/// <returns>true:同期成功 false:未開始・設定・ファイルまたは再生失敗</returns>
	bool Update(const CameraPose &camera, bool paused, std::string &error,
	            SceneObjectId *invalidObject = nullptr,
	            const SceneAudioActions &actions = {});

	/// <summary>
	/// シーンが所有する再生だけを破棄する。共有音声と別のプレビューは保持する
	/// </summary>
	void Stop();

  private:
	struct Entry {
		AudioSourceComponent component;
		AudioClipHandle clip;
		AudioPlaybackSettings playback;
		AudioPlaybackHandle handle;
		bool pending = false;
	};

	/// <summary>
	/// 音源一覧を検証・読み込みし、必要な差分を所有再生へ反映する
	/// </summary>
	/// <param name="frame">収集済みの有効な音源と姿勢</param>
	/// <param name="paused">一時停止状態</param>
	/// <param name="restart">以前の再生を引き継がず最初から始めるか</param>
	/// <param name="error">失敗理由</param>
	/// <param name="invalidObject">問題のIDの出力先</param>
	/// <param name="actions">再生前に反映する、確定済みの要求</param>
	/// <returns>true:成功 false:音声の読み込み・同期失敗</returns>
	bool Synchronize(const SceneAudioFrame &frame, bool paused, bool restart,
	                 std::string &error, SceneObjectId *invalidObject,
	                 const SceneAudioActions &actions);

	const Scene &scene_;
	AudioClipManager &clips_;
	AudioSystem &audio_;
	std::map<SceneObjectId, Entry> entries_;
	std::uint64_t deviceRevision_ = 0;
	std::optional<std::uint64_t> actionRevision_;
	bool started_ = false;
	bool paused_ = false;
};
