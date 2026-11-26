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

  private:
	friend class AudioClipManager;
	friend class AudioSystem;
	std::shared_ptr<const WaveData> data_;
};
