#pragma once

#include <array>
#include <cstddef>

/// <summary>
/// 直近のフレーム間隔を固定容量で記録し、集計する
/// </summary>
class FrameTimingHistory {
  public:
	static constexpr std::size_t kCapacity = 240;
	static constexpr float kLongIntervalSeconds = 1.0f;

	/// <summary>
	/// 記録中のフレーム間隔の集計。時間は名前に示した単位を使用する
	/// </summary>
	struct Summary {
		std::size_t sampleCount = 0;
		std::size_t longIntervalCount = 0;
		double durationSeconds = 0.0;
		double framesPerSecond = 0.0;
		double meanMilliseconds = 0.0;
		double minimumMilliseconds = 0.0;
		double maximumMilliseconds = 0.0;
		double percentile95Milliseconds = 0.0;
	};

	/// <summary>
	/// フレーム間隔を追加する。容量を超えると最も古い記録を置き換える
	/// </summary>
	/// <param name="seconds">有限で正の経過時間（秒）</param>
	/// <returns>true:追加成功 false:値が無効。記録は保持する</returns>
	bool AddSample(float seconds);

	/// <summary>
	/// すべての記録をクリアする
	/// </summary>
	void Clear();

	/// <summary>
	/// 現在の記録を集計する。FPSはフレーム数を合計時間で割って求める
	/// </summary>
	/// <returns>集計値。記録がない場合はすべて0。95パーセンタイルは記録の95%以上がその値以下になる最小の記録値</returns>
	Summary GetSummary() const;

	/// <summary>
	/// 古い順で指定したフレーム間隔を取得する
	/// </summary>
	/// <param name="index">0から記録数未満の位置</param>
	/// <returns>経過時間（秒）。範囲外の場合は例外</returns>
	float GetSampleSeconds(std::size_t index) const;

  private:
	std::array<float, kCapacity> samples_{};
	std::size_t nextIndex_ = 0;
	std::size_t count_ = 0;
};
