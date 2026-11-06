#pragma once

#include "audio/AudioClipHandle.h"
#include "audio/AudioPlaybackHandle.h"
#include "audio/AudioSystem.h"
#include <filesystem>
#include <string>

class AudioClipManager;

/// <summary>
/// シーンと独立した1つの試聴を保持し、他の再生を変更せず停止・解放する
/// </summary>
class AudioPreview {
  public:
	/// <summary>
	/// 試聴の終了まで存続する音声データ管理と再生基盤を受け取る
	/// </summary>
	/// <param name="clips">EditorとPlayで共有するWAVキャッシュ</param>
	/// <param name="audio">初期化済みの再生基盤</param>
	AudioPreview(AudioClipManager &clips, AudioSystem &audio);
	~AudioPreview();
	AudioPreview(const AudioPreview &) = delete;
	AudioPreview &operator=(const AudioPreview &) = delete;

	/// <summary>
	/// WAVをキャッシュからではなく読み直す。成功時だけ試聴を停止し、参照を置き換える
	/// </summary>
	/// <param name="path">実行ファイル基準または絶対パス</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:読み込み成功 false:失敗。既存の音声と再生は保持する</returns>
	bool Load(const std::filesystem::path &path, std::string &error);

	/// <summary>
	/// 設定した音量とループで、2D音声を先頭から試聴する。現在の試聴は先に停止する
	/// </summary>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:再生開始 false:音声なし・デバイス不在・再生失敗</returns>
	bool Play(std::string &error);

	/// <summary>
	/// 試聴だけを一時停止する
	/// </summary>
	/// <returns>true:一時停止済み false:停止中または処理失敗</returns>
	bool Pause();

	/// <summary>
	/// 試聴だけを一時停止位置から再開する
	/// </summary>
	/// <returns>true:再生中 false:停止中または処理失敗</returns>
	bool Resume();

	/// <summary>
	/// 自分の試聴を停止する。読み込み済み音声と試聴設定は保持する
	/// </summary>
	void Stop();

	/// <summary>
	/// 試聴だけの音量を変更する。無効値や処理失敗時は現在の値を保持する
	/// </summary>
	/// <param name="volume">0から4の有限値</param>
	/// <returns>true:設定成功 false:値が無効または処理失敗</returns>
	bool SetVolume(float volume);

	/// <summary>
	/// 停止中に次の試聴のループを設定する。再生中は同値だけ受け付ける
	/// </summary>
	/// <param name="loop">全体を繰り返すか</param>
	/// <returns>true:設定成功 false:再生または一時停止中の変更</returns>
	bool SetLoop(bool loop);

	/// <summary>
	/// 自然終了とデバイス再取得を反映した試聴状態を取得する
	/// </summary>
	/// <returns>停止・再生・一時停止</returns>
	AudioPlaybackState GetState() const;

	/// <summary>
	/// 試聴中の音声内の位置を取得する。ループでは現在の周回内を返す
	/// </summary>
	/// <returns>秒数。停止中は0</returns>
	double GetPositionSeconds() const;

	/// <summary>
	/// 試聴だけに適用する音量を取得する
	/// </summary>
	/// <returns>0から4の音量</returns>
	float GetVolume() const;

	/// <summary>
	/// 次の試聴に使用するループ設定を取得する
	/// </summary>
	/// <returns>true:繰り返す false:1回だけ再生</returns>
	bool GetLoop() const;

	/// <summary>
	/// 読み込み済み音声を取得する。停止や再読み込み失敗では保持する
	/// </summary>
	/// <returns>共有参照。音声なしは空</returns>
	const AudioClipHandle &GetClip() const;

	/// <summary>
	/// 最後に読み込みに成功した音声パスを取得する
	/// </summary>
	/// <returns>成功した参照。音声なしは空</returns>
	const std::filesystem::path &GetPath() const;

	/// <summary>
	/// 自分の試聴を停止し、音声・パス・設定を初期値へ戻す
	/// </summary>
	void Clear();

  private:
	AudioClipManager &clips_;
	AudioSystem &audio_;
	AudioClipHandle clip_;
	AudioPlaybackHandle voice_;
	std::filesystem::path path_;
	AudioPlaybackSettings settings_;
};
