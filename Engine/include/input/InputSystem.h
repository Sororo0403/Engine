#pragma once

#include "input/Gamepad.h"
#include "input/IInputBackend.h"
#include "input/Keyboard.h"
#include "input/Mouse.h"

#include <memory>

class InputSystem {
  public:
	~InputSystem();

	/// <summary>
	/// バックエンドの所有権を受け取り、初期化する
	/// </summary>
	/// <param name="backend">所有権を渡す入力バックエンド。初期化失敗時も呼び出し先で破棄される</param>
	/// <returns>true:成功 false:初期化済み、バックエンドが未指定、または初期化失敗</returns>
	bool Initialize(std::unique_ptr<IInputBackend> backend);

	/// <summary>
	/// 各デバイスの入力状態を更新する
	/// </summary>
	/// <returns>true:更新成功（入力のリセットを含む） false:未初期化、または取得失敗</returns>
	bool Update();

	/// <summary>
	/// 入力状態をクリアしてバックエンドを終了する
	/// </summary>
	void Finalize();

	/// <summary>
	/// キーボードの状態を取得する
	/// </summary>
	/// <returns>この入力システムが所有するキーボードへの読み取り専用参照</returns>
	const Keyboard &GetKeyboard() const;

	/// <summary>
	/// 最新の入力更新でアプリが操作を受け付けているか確認する
	/// </summary>
	/// <returns>true:入力可能 false:未初期化、取得失敗、または非アクティブ</returns>
	bool IsActive() const {
		return active_;
	}

	/// <summary>
	/// コントローラーの状態を取得する
	/// </summary>
	/// <returns>この入力システムが所有するコントローラーへの読み取り専用参照</returns>
	const Gamepad &GetGamepad() const;

	/// <summary>
	/// マウスの状態を取得する
	/// </summary>
	/// <returns>この入力システムが所有するマウスへの読み取り専用参照</returns>
	const Mouse &GetMouse() const;

  private:
	std::unique_ptr<IInputBackend> backend_;
	bool active_ = false;

	Keyboard keyboard_;
	Mouse mouse_;
	Gamepad gamepad_;
};
