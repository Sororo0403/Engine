#pragma once

#include "audio/AudioClipHandle.h"
#include "audio/AudioEmitterChange.h"
#include "audio/AudioPlaybackHandle.h"
#include "audio/AudioSettings.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
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
	/// 受信姿勢と複数の音源位置を一括設定する。大きく移動しても途中の位置関係を検証に使わない
	/// </summary>
	/// <param name="listener">変更後の左手座標系の受信姿勢</param>
	/// <param name="changes">重複しない有効な空間再生と、その新しい位置・距離</param>
	/// <returns>true:一括設定成功 false:対象・重複・姿勢・距離が無効。失敗時は全設定を保持する</returns>
	bool SetSpatialState(const AudioListenerPose &listener,
	                     std::span<const AudioEmitterChange> changes);

	/// <summary>
	/// 全音声処理の進行を一時停止・再開する。個別の再生状態とバッファは保持する
	/// </summary>
	/// <param name="suspended">true:処理を止める false:処理を再開する</param>
	/// <returns>true:成功または同値 false:未初期化またはネイティブ処理失敗</returns>
	bool SetSuspended(bool suspended);

	/// <summary>
	/// デバイスの初期化・再取得・終了による再生破棄の世代を取得する
	/// </summary>
	/// <returns>同じ値の間はデバイスの世代が同じ。初期値は0</returns>
	std::uint64_t GetDeviceRevision() const;

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
