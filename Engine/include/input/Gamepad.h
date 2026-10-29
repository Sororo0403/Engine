#pragma once

#include "input/GamepadState.h"

class Gamepad {
  public:
	/// <summary>
	/// 取得済みの状態を反映し、デッドゾーンを適用する
	/// </summary>
	/// <param name="state">バックエンドが取得した入力状態。未接続の場合は現在と前フレームの状態をクリアする</param>
	void Update(const GamepadState &state);

	/// <summary>
	/// コントローラーが接続されているか確認する
	/// </summary>
	/// <returns>true:接続されている false:接続されていない</returns>
	bool IsConnected() const;

	/// <summary>
	/// ボタンが押されているか確認する
	/// </summary>
	/// <param name="button">判定する共通ボタン</param>
	/// <returns>true:押されている false:押されていない、未接続、またはボタンが無効</returns>
	bool IsDown(GamepadButton button) const;

	/// <summary>
	/// 今フレームで押されたボタンか確認する
	/// </summary>
	/// <param name="button">判定する共通ボタン</param>
	/// <returns>true:前フレームは離されていて、今フレームは押されている false:それ以外</returns>
	bool IsPressed(GamepadButton button) const;

	/// <summary>
	/// 今フレームで離されたボタンか確認する
	/// </summary>
	/// <param name="button">判定する共通ボタン</param>
	/// <returns>true:前フレームは押されていて、今フレームは離されている false:それ以外。未接続時はfalse</returns>
	bool IsReleased(GamepadButton button) const;

	/// <summary>
	/// デッドゾーン適用後の入力状態を取得する
	/// </summary>
	/// <returns>このコントローラーが保持する入力状態への読み取り専用参照</returns>
	const GamepadState &GetState() const;

	/// <summary>
	/// 現在と前フレームの状態をクリアする
	/// </summary>
	void Reset();

  private:
	/// <summary>
	/// スティック中央付近の小さな入力を除去する
	/// </summary>
	/// <param name="stick">デッドゾーン適用前のスティック入力値</param>
	/// <returns>方向を保ち、入力の強さを0～1に調整した値。デッドゾーン内は両成分が0</returns>
	static GamepadStick ApplyDeadZone(GamepadStick stick);

	static constexpr float kStickDeadZone = 0.15f;

	GamepadState state_{};
	GamepadState previousState_{};
};
