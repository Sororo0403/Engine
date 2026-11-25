#pragma once

#include "gameplay/GameActionState.h"
#include <array>
#include <vector>

class Keyboard;
class Mouse;
class Gamepad;

/// <summary>
/// Editorと出力ゲームで共通の名前付き操作を取得し、機器を合成した押下・解放を保持する
/// </summary>
class GameActionReader {
  public:
	/// <summary>
	/// 機器ごとの入力許可を反映して取得する。開始・再接続・許可復帰時は一度離すまで反映しない
	/// </summary>
	/// <param name="bindings">検証済みの設定。変更した操作だけ履歴を初期化する</param>
	/// <param name="keyboard">今回のキー状態</param>
	/// <param name="mouse">今回のマウス状態。nullは無効</param>
	/// <param name="gamepad">今回のゲームパッド状態。nullは無効</param>
	/// <param name="enabled">ゲーム操作を許可する。falseでは全状態を解除する</param>
	/// <param name="pointerEnabled">マウス操作を許可する。Game画像の外ではfalseを渡す</param>
	/// <returns>次のRead・Resetまで有効な一覧。無効な設定は空。入力禁止中も操作名は保持する</returns>
	std::span<const GameActionState>
	Read(std::span<const GameActionBinding> bindings, const Keyboard &keyboard,
	     const Mouse *mouse, const Gamepad *gamepad, bool enabled = true,
	     bool pointerEnabled = true, float pressThreshold = .6f,
	     float releaseThreshold = .4f);

	/// <summary>
	/// 操作の履歴を解放し、次の取得を開始直後として扱う
	/// </summary>
	void Reset();

  private:
	struct History {
		GameActionBinding binding;
		std::array<bool, 4> blocked{true, true, true, true};
		std::array<bool, 4> held{};
		bool down = false;
	};

	std::vector<History> history_;
	std::vector<GameActionState> states_;
};
