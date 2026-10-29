#pragma once

#include "input/IInputBackend.h"
#include "input/Keyboard.h"

#include <memory>

class InputSystem {
  public:
	~InputSystem();

	/// <summary>
	/// バックエンドの所有権を受け取り、初期化する
	/// </summary>
	bool Initialize(std::unique_ptr<IInputBackend> backend);

	/// <summary>
	/// 各デバイスの入力状態を更新する
	/// </summary>
	bool Update();

	/// <summary>
	/// 入力状態をクリアしてバックエンドを終了する
	/// </summary>
	void Finalize();

	/// <summary>
	/// キーボードの状態を取得する
	/// </summary>
	const Keyboard &GetKeyboard() const;

  private:
	std::unique_ptr<IInputBackend> backend_;

	Keyboard keyboard_;
};
