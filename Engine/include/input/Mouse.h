#pragma once

#include "input/MouseState.h"

/// <summary>
/// マウスの現在と前フレームの入力状態を管理する
/// </summary>
class Mouse {
  public:
	/// <summary>
	/// バックエンドが取得した入力状態を反映する
	/// </summary>
	/// <param name="state">取得した状態。未接続の場合は状態をクリアする</param>
	void Update(const MouseState &state);

	/// <summary>
	/// マウスの入力状態を取得できているか確認する
	/// </summary>
	/// <returns>true:取得済み false:未接続、または入力なし</returns>
	bool IsConnected() const;

	/// <summary>
	/// ボタンが押されているか確認する
	/// </summary>
	/// <param name="button">判定するボタン</param>
	/// <returns>true:押されている false:それ以外</returns>
	bool IsDown(MouseButton button) const;

	/// <summary>
	/// 今フレームで押されたボタンか確認する
	/// </summary>
	/// <param name="button">判定するボタン</param>
	/// <returns>true:押された false:それ以外</returns>
	bool IsPressed(MouseButton button) const;

	/// <summary>
	/// 今フレームで離されたボタンか確認する
	/// </summary>
	/// <param name="button">判定するボタン</param>
	/// <returns>true:離された false:それ以外</returns>
	bool IsReleased(MouseButton button) const;

	/// <summary>
	/// 最新の入力状態を取得する
	/// </summary>
	/// <returns>このマウスが保持する状態への読み取り専用参照</returns>
	const MouseState &GetState() const;

	/// <summary>
	/// 現在と前フレームの入力状態をクリアする
	/// </summary>
	void Reset();

  private:
	MouseState state_{};

	std::array<bool, static_cast<std::size_t>(MouseButton::COUNT)>
	    previousButtons_{};
};
