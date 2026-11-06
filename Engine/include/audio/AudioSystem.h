#pragma once

#include "audio/AudioClipHandle.h"
#include "audio/AudioPlaybackHandle.h"
#include "audio/AudioSettings.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

/// <summary>
/// 1回の音声再生の状態
/// </summary>
enum class AudioPlaybackState { STOPPED, PLAYING, PAUSED };

/// <summary>
/// 音声データを共有し、デバイスと独立した再生を管理する。初期化済みCOMを持つメインスレッドで使用する
/// </summary>
class AudioSystem {
  public:
	static constexpr std::size_t kMaximumVoices = 64;
	AudioSystem();
	~AudioSystem();
	AudioSystem(const AudioSystem &) = delete;
	AudioSystem &operator=(const AudioSystem &) = delete;

	/// <summary>
	/// 音声基盤を初期化する。出力デバイスがない場合も無音状態として初期化できる
	/// </summary>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:初期化成功 false:初期化済み、COM未初期化、または基盤作成失敗</returns>
	bool Initialize(std::string &error);

	/// <summary>
	/// 全再生を破棄して出力デバイスを取得し直す。古い再生ハンドルは無効になる
	/// </summary>
	/// <param name="error">デバイス不在・失敗時の理由</param>
	/// <returns>true:出力デバイスあり false:未初期化、デバイス不在、または再取得失敗</returns>
	bool ResetDevice(std::string &error);

	/// <summary>
	/// 終了した再生を回収し、空間配置を反映する。無音状態自体はエラーにしない
	/// </summary>
	/// <param name="error">成功時は空、デバイス喪失などの失敗理由</param>
	/// <returns>true:更新成功 false:未初期化、デバイス障害、または空間配置失敗</returns>
	bool Update(std::string &error);

	/// <summary>
	/// 再生中の音を先に停止・破棄してからデバイスを解放する
	/// </summary>
	void Finalize();

	/// <summary>
	/// 現在音を出せるデバイスがあるか確認する
	/// </summary>
	/// <returns>true:出力可能 false:未初期化・無音・デバイス障害</returns>
	bool IsAvailable() const;

	/// <summary>
	/// 現在のデバイスの出力チャンネル数を取得する
	/// </summary>
	/// <returns>チャンネル数。出力不可の場合は0</returns>
	std::uint32_t GetOutputChannels() const;

	/// <summary>
	/// 音声を最初から独立して再生する。再生が終わるまで共有データを保持する
	/// </summary>
	/// <param name="clip">同じデバイスに依存しない有効な音声</param>
	/// <param name="settings">音量・ループ・空間配置。空間配置はモノラルのみ</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>成功:この再生だけを指すハンドル 失敗:未設定</returns>
	AudioPlaybackHandle Play(const AudioClipHandle &clip,
	                         const AudioPlaybackSettings &settings,
	                         std::string &error);

	/// <summary>
	/// 再生状態を取得する。停止・自然終了・別基盤・古い世代は停止として返す
	/// </summary>
	/// <param name="handle">確認する再生</param>
	/// <returns>現在の状態</returns>
	AudioPlaybackState GetState(AudioPlaybackHandle handle) const;

	/// <summary>
	/// 元音声の周波数で処理したフレーム数を取得する。ループでは合計、停止や無効なハンドルは0
	/// </summary>
	/// <param name="handle">確認する再生</param>
	/// <returns>再生を開始してからのフレーム数。一時停止では保持する</returns>
	std::uint64_t GetPlayedFrames(AudioPlaybackHandle handle) const;

	/// <summary>
	/// 再生位置とバッファを保持して一時停止する
	/// </summary>
	/// <param name="handle">この基盤の有効な再生</param>
	/// <returns>true:一時停止済み false:停止・無効・ネイティブ処理失敗</returns>
	bool Pause(AudioPlaybackHandle handle);

	/// <summary>
	/// 一時停止位置から再開する
	/// </summary>
	/// <param name="handle">この基盤の有効な再生</param>
	/// <returns>true:再生中 false:停止・無効・ネイティブ処理失敗</returns>
	bool Resume(AudioPlaybackHandle handle);

	/// <summary>
	/// 1回の再生を破棄する。同じ音声のほかの再生には影響しない
	/// </summary>
	/// <param name="handle">停止する再生</param>
	/// <returns>true:破棄成功 false:無効または破棄済み</returns>
	bool Stop(AudioPlaybackHandle handle);

	/// <summary>
	/// 全再生を破棄し、発行済みハンドルを無効にする
	/// </summary>
	void StopAll();

	/// <summary>
	/// 1回の再生の音量を変更する
	/// </summary>
	/// <param name="handle">この基盤の有効な再生</param>
	/// <param name="volume">0から4の有限値</param>
	/// <returns>true:成功 false:対象・音量が無効、または処理失敗</returns>
	bool SetVolume(AudioPlaybackHandle handle, float volume);

	/// <summary>
	/// 全再生に掛ける音量を変更する。0は無音だが再生位置は進む
	/// </summary>
	/// <param name="volume">0から4の有限値</param>
	/// <returns>true:成功 false:未初期化、値が無効、または処理失敗</returns>
	bool SetMasterVolume(float volume);

	/// <summary>
	/// 受信姿勢を次の更新に設定する。既存の空間音源との距離が無効なら現在値を保持する
	/// </summary>
	/// <param name="listener">左手座標系の有効な姿勢</param>
	/// <returns>true:成功 false:姿勢または距離が無効</returns>
	bool SetListener(const AudioListenerPose &listener);

	/// <summary>
	/// 空間音源の位置と減衰距離を次の更新に設定する
	/// </summary>
	/// <param name="handle">空間配置を使用しているモノラルの再生</param>
	/// <param name="emitter">有効な位置と距離</param>
	/// <returns>true:成功 false:対象・設定・受信位置との距離が無効</returns>
	bool SetEmitter(AudioPlaybackHandle handle,
	                const AudioEmitterSettings &emitter);

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
