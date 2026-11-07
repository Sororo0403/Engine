#pragma once

#include "gameplay/GameActionBinding.h"

/// <summary>
/// 一回の入力取得における名前付き操作の状態。機器を追加で押しても押下を重複させない
/// </summary>
struct GameActionState {
	std::string name;
	bool down = false;
	bool pressed = false;
	bool released = false;
	// 押下閾値と独立した入力の強さ。複数の割り当ては最大値を使う。
	float value = down ? 1.f : 0.f;

	/// <summary>
	/// 件数・名前・強さ・状態の整合性を確認する
	/// </summary>
	/// <param name="states">一回の入力取得結果</param>
	/// <returns>true:有効 false:名前の重複や押下・解放状態の矛盾がある</returns>
	static bool IsValid(std::span<const GameActionState> states);
};
