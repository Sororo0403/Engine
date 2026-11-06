#pragma once

#include "audio/AudioClipHandle.h"
#include "audio/AudioPlaybackHandle.h"
#include "audio/AudioSystem.h"
#include "scene/SceneAudio.h"
#include <map>

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
	/// <returns>true:開始成功 false:無効なシーン・音声またはデバイス処理失敗</returns>
	bool Start(const CameraPose &camera, bool paused, std::string &error,
	           SceneObjectId *invalidObject = nullptr);

	/// <summary>
	/// 成功したシミュレーションの有効状態・ワールド位置・一時停止状態を音へ反映する
	/// </summary>
	/// <param name="camera">現在の描画姿勢</param>
	/// <param name="paused">音声の再生位置を止めるか。1フレーム実行中もtrueを渡す</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <param name="invalidObject">指定時は問題のID</param>
	/// <returns>true:同期成功 false:未開始・設定・ファイルまたは再生失敗</returns>
	bool Update(const CameraPose &camera, bool paused, std::string &error,
	            SceneObjectId *invalidObject = nullptr);

	/// <summary>
	/// シーンが所有する再生だけを破棄する。共有音声と別のプレビューは保持する
	/// </summary>
	void Stop();

	/// <summary>
	/// 有効な音源を最初から手動再生する。Play on Startが無効な音源も対象にできる
	/// </summary>
	/// <param name="id">収集済みの有効な音源</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:再生開始またはデバイス復帰待ち false:未開始・対象不在・再生失敗</returns>
	bool PlaySource(SceneObjectId id, std::string &error);

	/// <summary>
	/// 有効な音源を手動停止し、再生開始待ちも取り消す。再有効化までは自動再開しない
	/// </summary>
	/// <param name="id">収集済みの音源</param>
	/// <returns>true:停止済み false:対象なし</returns>
	bool StopSource(SceneObjectId id);

	/// <summary>
	/// 物体の再生状態を取得する
	/// </summary>
	/// <param name="id">このシーンの音源</param>
	/// <returns>現在の状態。無効・未割り当て・終了済みは停止</returns>
	AudioPlaybackState GetState(SceneObjectId id) const;

	/// <summary>
	/// 音源の再生位置を元の周波数のフレーム数で取得する
	/// </summary>
	/// <param name="id">このシーンの音源</param>
	/// <returns>処理済みフレーム数。再生なしの場合は0</returns>
	std::uint64_t GetPlayedFrames(SceneObjectId id) const;

	/// <summary>
	/// 開始済みか確認する。無音や全音源の自然終了後も開始済みとして扱う
	/// </summary>
	/// <returns>true:開始済み false:停止中</returns>
	bool IsStarted() const {
		return started_;
	}

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
	/// <returns>true:成功 false:音声の読み込み・同期失敗</returns>
	bool Synchronize(const SceneAudioFrame &frame, bool paused, bool restart,
	                 std::string &error, SceneObjectId *invalidObject);

	const Scene &scene_;
	AudioClipManager &clips_;
	AudioSystem &audio_;
	std::map<SceneObjectId, Entry> entries_;
	std::uint64_t deviceRevision_ = 0;
	bool started_ = false;
	bool paused_ = false;
};
