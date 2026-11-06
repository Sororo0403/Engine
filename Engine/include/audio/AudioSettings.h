#pragma once

#include <DirectXMath.h>

/// <summary>
/// 左手座標系での音の受信位置と向き。前方は+Z、上方は+Yを標準とする
/// </summary>
struct AudioListenerPose {
	DirectX::XMFLOAT3 position{};
	DirectX::XMFLOAT3 forward{0, 0, 1};
	DirectX::XMFLOAT3 up{0, 1, 0};

	/// <summary>
	/// 位置が有限で、前方と上方が正規直交しているか確認する
	/// </summary>
	/// <returns>true:有効 false:非有限、非単位、または直交していない</returns>
	bool IsValid() const;
};

/// <summary>
/// 全方向に広がるモノラル音源の位置と、減衰が始まる距離・無音になる距離
/// </summary>
struct AudioEmitterSettings {
	static constexpr float kMaximumDistance = 100000.0f;
	DirectX::XMFLOAT3 position{};
	float minDistance = 1.0f;
	float maxDistance = 50.0f;

	/// <summary>
	/// 位置と距離が有限で、0以上の最短距離とそれより大きな最大距離か確認する
	/// </summary>
	/// <returns>true:有効 false:非有限または範囲外</returns>
	bool IsValid() const;
};

/// <summary>
/// 1回の再生の音量・ループ・空間配置を指定する。空間配置はモノラル音声だけに対応する
/// </summary>
struct AudioPlaybackSettings {
	static constexpr float kMaximumVolume = 4.0f;
	float volume = 1.0f;
	bool loop = false;
	bool spatial = false;
	bool startPaused = false;
	AudioEmitterSettings emitter{};

	/// <summary>
	/// 音量と、空間配置を使う場合の距離設定が有効か確認する
	/// </summary>
	/// <returns>true:有効 false:音量または音源設定が無効</returns>
	bool IsValid() const;
};
