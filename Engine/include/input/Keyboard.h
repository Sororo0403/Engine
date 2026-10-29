#pragma once

#include "input/Key.h"

#include <array>

class Keyboard {
  public:
	using State = std::array<bool, static_cast<std::size_t>(Key::COUNT)>;

	/// <summary>
	/// 取得済みのキー状態で更新する
	/// </summary>
	/// <param name="states">共通キーごとの押下状態</param>
	void Update(const State &states);

	/// <summary>
	/// キーが押されているか確認する
	/// </summary>
	/// <param name="key">判定する共通キー</param>
	/// <returns>true:押されている false:押されていない、またはキーが無効</returns>
	bool IsDown(Key key) const;

	/// <summary>
	/// 今フレームで押されたキーか確認する
	/// </summary>
	/// <param name="key">判定する共通キー</param>
	/// <returns>true:前フレームは離されていて、今フレームは押されている false:それ以外</returns>
	bool IsPressed(Key key) const;

	/// <summary>
	/// 今フレームで離されたキーか確認する
	/// </summary>
	/// <param name="key">判定する共通キー</param>
	/// <returns>true:前フレームは押されていて、今フレームは離されている false:それ以外</returns>
	bool IsReleased(Key key) const;

	/// <summary>
	/// 現在と前フレームのキー状態をクリアする
	/// </summary>
	void Reset();

  private:
	State keys_{};
	State previousKeys_{};
};
