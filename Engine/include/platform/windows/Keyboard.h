#pragma once

#include <Windows.h>
#include <array>

class Keyboard {
  public:
	/// <summary>
	/// キーの押下状態を更新する
	/// </summary>
	/// <param name="hwnd">入力を受け付けるウィンドウ</param>
	void Update(HWND hwnd);

	/// <summary>
	/// キーが押されているか確認する
	/// </summary>
	/// <param name="key">仮想キーコード</param>
	/// <returns>true:押されている false:押されていない</returns>
	bool IsDown(int key) const;

  private:
	std::array<bool, 256> keys_{};
};
