#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

/// <summary>
/// 再生・一時停止・1フレーム実行のゲーム時間を管理する
/// </summary>
class PlaybackClock {
  public:
	/// <summary>
	/// 一時停止と未処理のフレーム実行を解除し、再生速度を1倍へ戻す
	/// </summary>
	void Reset() {
		paused_ = false;
		stepRequested_ = false;
		timeScale_ = 1.0f;
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
	bool IsPaused() const {
		return paused_;
	}

	/// <summary>
	/// 再生速度を変更する。一時停止状態と予約済みのStepは保持する
	/// </summary>
	/// <param name="scale">有限な0.1倍から1倍までの速度</param>
	/// <returns>true:設定成功 false:値が無効。以前の速度は保持する</returns>
	bool SetTimeScale(float scale) {
		if (!std::isfinite(scale) || scale < kMinimumTimeScale ||
		    scale > 1.0f) {
			return false;
		}

		timeScale_ = scale;
		return true;
	}

	/// <summary>
	/// 現在の再生速度を取得する
	/// </summary>
	/// <returns>0.1倍から1倍までの速度</returns>
	float GetTimeScale() const {
		return timeScale_;
	}

	/// <summary>
	/// 今回進めるゲーム時間を取得し、予約した1フレーム実行を消費する
	/// </summary>
	/// <param name="seconds">実フレームの経過秒数</param>
	/// <returns>通常時は実時間を0.1秒で制限して速度を掛けた値。Stepは速度によらず1/60秒。一時停止中や無効な入力はnullopt</returns>
	std::optional<float> Advance(float seconds) {
		if (!std::isfinite(seconds) || seconds < 0) {
			return std::nullopt;
		}

		if (!paused_) {
			return (std::min)(seconds, kMaximumFrameSeconds) * timeScale_;
		}

		if (!stepRequested_) {
			return std::nullopt;
		}

		stepRequested_ = false;
		return kStepSeconds;
	}

	static constexpr float kStepSeconds = 1.0f / 60.0f;
	static constexpr float kMinimumTimeScale = 0.1f;
	static constexpr float kMaximumFrameSeconds = 0.1f;

  private:
	bool paused_ = false;
	bool stepRequested_ = false;
	float timeScale_ = 1.0f;
};
