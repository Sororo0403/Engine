#pragma once
#include "math/Transform.h"
#include <cstddef>
#include <vector>

/// <summary>
/// 開始時のローカル姿勢に対する移動量・回転・拡大率のキーフレーム
/// </summary>
struct TransformAnimationKey {
	float seconds = 0;
	Transform offset;

	/// <summary>
	/// 時刻と姿勢が一致するか確認する。回転の符号反転は同じ向きとして扱う
	/// </summary>
	/// <param name="other">比較するキー</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const TransformAnimationKey &other) const;
};

/// <summary>
/// 物体の開始姿勢を基準に、平行移動・回転・拡縮を再生する編集データ
/// </summary>
struct TransformAnimationComponent {
	enum class Mode { ONCE, LOOP, PING_PONG };
	enum class Interpolation { LINEAR, SMOOTH, STEP };
	static constexpr std::size_t kMaximumKeys = 64;
	static constexpr float kMaximumSeconds = 3600;
	static constexpr float kMaximumOffset = 100000;
	static constexpr float kMinimumScale = .001f;
	static constexpr float kMaximumScale = 1000;
	static constexpr float kMaximumSpeed = 16;
	std::vector<TransformAnimationKey> keys{
	    {0, {}}, {1, {{1, 1, 1}, {0, 0, 0, 1}, {0, 1, 0}}}};
	Mode mode = Mode::ONCE;
	Interpolation interpolation = Interpolation::LINEAR;
	float speed = 1;
	bool playOnStart = true;

	/// <summary>
	/// 件数・時刻の昇順・再生設定と、0を通過しない拡縮を確認する
	/// </summary>
	/// <returns>true:有効 false:設定・時刻・姿勢が無効</returns>
	bool IsValid() const;

	/// <summary>
	/// 最後のキーの時刻を取得する
	/// </summary>
	/// <returns>クリップの秒数。キーなしは0</returns>
	double GetDuration() const;

	/// <summary>
	/// 再生方式に従い、経過時間をクリップ内の時刻へ変換する
	/// </summary>
	/// <param name="seconds">有限な0以上の再生時間</param>
	/// <returns>クリップ内の秒数。無効な時間・長さは0</returns>
	double GetClipTime(double seconds) const;

	/// <summary>
	/// 時刻に対応する相対姿勢を補間する。回転には最短経路の球面補間を使う
	/// </summary>
	/// <param name="seconds">速度を反映済みの有限な0以上の経過時間</param>
	/// <param name="offset">成功時だけ置き換える移動量・回転・倍率</param>
	/// <returns>true:成功 false:設定・時間・補間結果が無効</returns>
	bool Sample(double seconds, Transform &offset) const;

	/// <summary>
	/// キーと再生設定がすべて一致するか確認する
	/// </summary>
	/// <param name="other">比較するアニメーション</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const TransformAnimationComponent &other) const = default;
};
