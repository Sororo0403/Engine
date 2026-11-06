#pragma once
#include <string>

/// <summary>
/// 実行中に選んだクリップ・時刻・速度・再生指定。編集用の設定とは独立して保持する
/// </summary>
struct AnimatorPlaybackState {
	std::string clip;
	double seconds = 0, duration = 0;
	float speed = 1;
	bool loop = true, playing = false, applied = false;
};
