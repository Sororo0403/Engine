#pragma once

#include <cstddef>
#include <cstdint>

class AudioSystem;

/// <summary>
/// 1回の再生を識別する。停止・自然終了・デバイス再初期化後に別の再生へ再利用されない
/// </summary>
class AudioPlaybackHandle {
  public:
	AudioPlaybackHandle() = default;

	/// <summary>
	/// 再生時に発行されたハンドルか確認する。現在の再生状態はAudioSystemで取得する
	/// </summary>
	/// <returns>true:発行済み false:未設定</returns>
	explicit operator bool() const {
		return system_ != 0;
	}

	/// <summary>
	/// 同じ音声基盤での同じ再生世代を指しているか確認する
	/// </summary>
	/// <param name="other">比較する再生</param>
	/// <returns>true:同じ再生 false:異なる再生</returns>
	bool operator==(const AudioPlaybackHandle &other) const = default;

  private:
	friend class AudioSystem;
	std::uint64_t system_ = 0;
	std::size_t index_ = 0;
	std::uint64_t generation_ = 0;
};
