#pragma once

#include "input/Key.h"

#include <array>

class Keyboard {
  public:
	using State = std::array<bool, static_cast<std::size_t>(Key::COUNT)>;

	/// <summary>
	/// 取得済みのキー状態で更新する
	/// </summary>
	void Update(const State &states);

	/// <summary>
	/// キーが押されているか確認する
	/// </summary>
	bool IsDown(Key key) const;

	/// <summary>
	/// 今フレームで押されたキーか確認する
	/// </summary>
	bool IsPressed(Key key) const;

	/// <summary>
	/// 今フレームで離されたキーか確認する
	/// </summary>
	bool IsReleased(Key key) const;

	/// <summary>
	/// 現在と前フレームのキー状態をクリアする
	/// </summary>
	void Reset();

  private:
	State keys_{};
	State previousKeys_{};
};
