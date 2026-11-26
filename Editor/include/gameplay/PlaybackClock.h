#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

/// <summary>
/// 再生・一時停止・1フレーム実行のゲーム時間を管理する
/// </summary>
class PlaybackClock {
  public:
	// Stop and Start also discard any queued single-frame advance.
	void Stop() {
		state_ = &stopped_;
		stepRequested_ = false;
	}

	void Start(bool paused = false) {
		state_ = paused ? static_cast<const State *>(&paused_) : &playing_;
		stepRequested_ = false;
	}

	void TogglePause() {
		state_ = &state_->TogglePause(*this);
	}

	void RequestStep() {
		state_->RequestStep(*this);
	}

	bool IsPaused() const {
		return state_ == &paused_;
	}

	std::optional<float> Advance(float seconds) {
		if (!std::isfinite(seconds) || seconds < 0) {
			return std::nullopt;
		}

		return state_->Advance(*this, seconds);
	}

	static constexpr float kStepSeconds = 1.0f / 60.0f;
	static constexpr float kMaximumFrameSeconds = 0.1f;

  private:
	// States are immutable. The queued step belongs to each clock instance.
	struct State {
		virtual ~State() = default;
		virtual const State &TogglePause(PlaybackClock &) const {
			return *this;
		}
		virtual void RequestStep(PlaybackClock &) const {}
		virtual std::optional<float> Advance(PlaybackClock &, float) const = 0;
	};

	struct StoppedState final : State {
		std::optional<float> Advance(PlaybackClock &, float) const override {
			return std::nullopt;
		}
	};

	struct PlayingState final : State {
		const State &TogglePause(PlaybackClock &clock) const override {
			clock.stepRequested_ = false;
			return paused_;
		}

		std::optional<float> Advance(PlaybackClock &, float seconds) const override {
			return (std::min)(seconds, kMaximumFrameSeconds);
		}
	};

	struct PausedState final : State {
		const State &TogglePause(PlaybackClock &clock) const override {
			clock.stepRequested_ = false;
			return playing_;
		}

		void RequestStep(PlaybackClock &clock) const override {
			clock.stepRequested_ = true;
		}

		std::optional<float> Advance(PlaybackClock &clock, float) const override {
			if (!clock.stepRequested_) {
				return std::nullopt;
			}

			clock.stepRequested_ = false;
			return kStepSeconds;
		}
	};

	inline static const StoppedState stopped_;
	inline static const PlayingState playing_;
	inline static const PausedState paused_;

	const State *state_ = &stopped_;
	bool stepRequested_ = false;
};
