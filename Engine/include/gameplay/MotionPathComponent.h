#pragma once

#include <DirectXMath.h>
#include <array>
#include <cstddef>
#include <vector>

/// <summary>
/// 開始時のローカル位置を原点として、指定した経路を等しい区間時間で平行移動する
/// </summary>
struct MotionPathComponent {
	/// <summary>
	/// 終点で停止、最後から始点へ戻る循環、または経路を逆順にたどる往復
	/// </summary>
	enum class Mode { ONCE, LOOP, PING_PONG };
	std::vector<std::array<float, 3>> points{{0, 0, 0}, {0, 3, 0}};
	float segmentSeconds = 2.0f;
	Mode mode = Mode::PING_PONG;
	bool smooth = false;
	static constexpr std::size_t kMaximumPoints = 64;
	static constexpr float kMinimumSegmentSeconds = 0.05f;
	static constexpr float kMaximumSegmentSeconds = 3600.0f;
	static constexpr float kMaximumOffset = 100000.0f;

	/// <summary>
	/// 始点・点数・有限な座標と区間時間・再生方式を確認する
	/// </summary>
	/// <returns>true:有効 false:始点が原点でない、または値が範囲外</returns>
	bool IsValid() const;

	/// <summary>
	/// 経路の全区間を一巡する時間を取得する
	/// </summary>
	/// <returns>秒数。無効な設定は0</returns>
	double GetDuration() const;

	/// <summary>
	/// 経過時間に対応する、開始時のローカル位置からの移動量を求める
	/// </summary>
	/// <param name="seconds">有限な0以上の経過秒数</param>
	/// <param name="offset">成功時に置き換えるローカル移動量</param>
	/// <returns>true:計算成功 false:時間または設定が無効。失敗時は出力を保持する</returns>
	bool Sample(double seconds, DirectX::XMFLOAT3 &offset) const;

	/// <summary>
	/// 経路と再生設定が一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const MotionPathComponent &other) const = default;
};
