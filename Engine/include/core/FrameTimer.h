#pragma once

#include <chrono>

class FrameTimer {
  public:
	/// <summary>
	/// 計測の基準時刻を現在に戻す
	/// </summary>
	void Reset();

	/// <summary>
	/// 前回の計測からの経過時間を取得し、基準時刻を更新する
	/// </summary>
	/// <returns>経過時間（秒）</returns>
	float Tick();

  private:
	using Clock = std::chrono::steady_clock;

	Clock::time_point previousTime_ = Clock::now();
};
