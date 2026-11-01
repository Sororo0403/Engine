#pragma once

/// <summary>
/// 主人公の移動と重力の設定。距離はワールド座標、時間は秒を使用する
/// </summary>
struct CharacterSettings {
	float moveSpeed = 5.0f;
	float jumpSpeed = 7.0f;
	float gravity = 20.0f;
	float terminalSpeed = 30.0f;

	static constexpr float kMaximumSpeed = 1000.0f;
	static constexpr float kMaximumGravity = 1000.0f;

	/// <summary>
	/// 各値が有限で正、かつ設定範囲内か確認する
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
