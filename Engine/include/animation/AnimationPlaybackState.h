#pragma once

/// <summary>
/// 再生用データから取得する、クリップ内の位置と再生状態
/// </summary>
struct AnimationPlaybackState {
	double seconds = 0;
	bool playing = false;
};
