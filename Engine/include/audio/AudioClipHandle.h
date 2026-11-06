#pragma once

#include "audio/WaveData.h"
#include <memory>

class AudioClipManager;
class AudioSystem;

/// <summary>
/// 読み込み済み音声への共有参照。キャッシュの解除やデバイスの終了後もデータを保持する
/// </summary>
class AudioClipHandle {
  public:
	AudioClipHandle() = default;

	/// <summary>
	/// 音声データを参照しているか確認する
	/// </summary>
	/// <returns>true:参照あり false:空</returns>
	explicit operator bool() const {
		return data_ != nullptr;
	}

	/// <summary>
	/// 同じ不変の音声データを共有しているか確認する
	/// </summary>
	/// <param name="other">比較する共有参照</param>
	/// <returns>true:同じデータ false:別のデータ</returns>
	bool operator==(const AudioClipHandle &other) const = default;

	/// <summary>
	/// 音声のチャンネル数を取得する
	/// </summary>
	/// <returns>1または2。空の参照は0</returns>
	std::uint16_t GetChannels() const {
		return data_ ? data_->channels : 0;
	}

	/// <summary>
	/// 音声のサンプリング周波数を取得する
	/// </summary>
	/// <returns>1秒当たりのフレーム数。空の参照は0</returns>
	std::uint32_t GetSampleRate() const {
		return data_ ? data_->sampleRate : 0;
	}

	/// <summary>
	/// 元データのサンプルフレーム数を取得する
	/// </summary>
	/// <returns>チャンネル全体を1組としたフレーム数。空の参照は0</returns>
	std::uint64_t GetFrameCount() const {
		return data_ ? data_->GetFrameCount() : 0;
	}

	/// <summary>
	/// ループしない場合の再生時間を取得する
	/// </summary>
	/// <returns>秒数。空の参照は0</returns>
	double GetDurationSeconds() const {
		return data_ ? data_->GetDurationSeconds() : 0;
	}

  private:
	friend class AudioClipManager;
	friend class AudioSystem;
	std::shared_ptr<const WaveData> data_;
};
