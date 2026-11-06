#pragma once

#include "audio/AudioSettings.h"
#include <filesystem>

/// <summary>
/// 物体のワールド位置で再生するWAV音源。空の音声参照は動作なしとして保持する
/// </summary>
struct AudioSourceComponent {
	std::filesystem::path clipPath;
	float volume = 1.0f;
	bool loop = false;
	bool playOnStart = true;
	bool spatial = false;
	float minDistance = 1.0f;
	float maxDistance = 50.0f;

	/// <summary>
	/// 音声参照の構文と音量・距離を確認する。ファイルの存在と形式は再生開始前に確認する
	/// </summary>
	/// <returns>true:有効 false:未対応の参照または無効な設定</returns>
	bool IsValid() const;

	/// <summary>
	/// 全設定と音声参照が一致するか確認する
	/// </summary>
	/// <param name="other">比較する設定</param>
	/// <returns>true:一致 false:変更あり</returns>
	bool operator==(const AudioSourceComponent &other) const = default;
};
