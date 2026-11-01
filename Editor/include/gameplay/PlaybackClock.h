#pragma once
#include <cmath>
#include <optional>

/// <summary>
/// 再生・一時停止・1フレーム実行のゲーム時間を管理する
/// </summary>
class PlaybackClock {
  public:
	/// <summary>
	/// 一時停止と未処理のフレーム実行を解除する
	/// </summary>
	void Reset() {
		paused_ = false;
		stepRequested_ = false;
	}
	/// <summary>
	/// 一時停止と再開を切り替える。切り替え時は未処理のフレーム実行を破棄する
	/// </summary>
	void TogglePause() {
		paused_ = !paused_;
		stepRequested_ = false;
	}
	/// <summary>
	/// 一時停止中に1フレーム実行を予約する。同じ更新までの重複要求はまとめる
	/// </summary>
	void RequestStep() {
		if (paused_) {
			stepRequested_ = true;
		}
	}
	/// <summary>
	/// 一時停止しているか取得する
	/// </summary>
	/// <returns>true:一時停止 false:通常再生</returns>
	bool IsPaused() const { return paused_; }
	/// <summary>
	/// 今回進めるゲーム時間を取得し、予約した1フレーム実行を消費する
	/// </summary>
	/// <param name="seconds">実フレームの経過秒数</param>
	/// <returns>通常時は経過時間、フレーム実行時は1/60秒。一時停止中や無効な入力はnullopt</returns>
	std::optional<float> Advance(float seconds) {
		if (!std::isfinite(seconds) || seconds < 0) {
			return std::nullopt;
		}
		if (!paused_) {
			return seconds;
		}
		if (!stepRequested_) {
			return std::nullopt;
		}
		stepRequested_ = false;
		return kStepSeconds;
	}
	static constexpr float kStepSeconds = 1.0f / 60.0f;

  private:
	bool paused_ = false;
	bool stepRequested_ = false;
};
