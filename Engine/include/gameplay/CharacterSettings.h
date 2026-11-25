#pragma once

/// <summary>
/// 主人公の移動と重力の設定。距離はワールド座標、時間は秒を使用する
/// </summary>
struct CharacterSettings {
	float moveSpeed = 5.0f;
	float jumpSpeed = 7.0f;
	float gravity = 20.0f;
	float terminalSpeed = 30.0f;
	// 足場を離れた後のジャンプ猶予と、着地前の入力保持時間（秒）。0は無効
	float coyoteTime = 0.0f;
	float jumpBufferTime = 0.0f;
	// 接地中の移動後に足元を探す距離。0は無効。
	float groundSnapDistance = 0.0f;
	// 地上を歩くときに乗り越えられる段差の高さ。0は無効。
	float stepHeight = 0.0f;
	// 移動・ジャンプ入力を使うか。無効でも重力と接触を更新する
	bool inputEnabled = true;

	static constexpr float kMaximumSpeed = 1000.0f;
	static constexpr float kMaximumGravity = 1000.0f;
	static constexpr float kMaximumJumpGraceSeconds = 1.0f;
	static constexpr float kMaximumGroundSnapDistance = 2.0f;
	static constexpr float kMaximumStepHeight = 2.0f;

	/// <summary>
	/// 移動と重力が有限で正、ジャンプ猶予が有限で0以上、かつ設定範囲内か確認する
	/// </summary>
	/// <returns>true:有効 false:無効</returns>
	bool IsValid() const;

	/// <summary>
	/// 移動設定が一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const CharacterSettings &other) const = default;
};
