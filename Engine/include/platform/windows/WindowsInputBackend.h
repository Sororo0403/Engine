#pragma once

#include "input/IInputBackend.h"

#include <GameInput.h>
#include <Windows.h>
#include <wrl/client.h>

class WindowsInputBackend final : public IInputBackend {
  public:
	/// <summary>
	/// 入力を受け付けるウィンドウを指定する
	/// </summary>
	/// <param name="hwnd">入力対象のウィンドウハンドル。所有権は受け取らない</param>
	explicit WindowsInputBackend(HWND hwnd);

	~WindowsInputBackend() override;

	/// <summary>
	/// 対象ウィンドウを確認してGameInputを初期化する
	/// </summary>
	/// <returns>true:成功 false:初期化済み、ウィンドウが無効、または初期化失敗</returns>
	bool Initialize() override;

	/// <summary>
	/// GameInputから取得した状態を共通の入力状態へ変換する
	/// </summary>
	/// <param name="state">取得結果の出力先。フォーカスがない場合はactiveがfalseになる</param>
	/// <returns>true:更新成功（入力を受け付けない場合を含む） false:未初期化、または取得失敗</returns>
	bool Update(InputState &state) override;

	/// <summary>
	/// GameInputを解放する
	/// </summary>
	void Finalize() override;

  private:
	/// <summary>
	/// Windowsの仮想キーコードを共通のキーに変換する
	/// </summary>
	/// <param name="virtualKey">Windowsの仮想キーコード</param>
	/// <returns>対応する共通キー。未対応の場合はKey::UNKNOWN</returns>
	static Key ToKey(unsigned int virtualKey);

	/// <summary>
	/// キーボードの入力状態を取得する
	/// </summary>
	/// <param name="states">共通キーごとの押下状態の出力先</param>
	/// <returns>true:取得成功、または入力なし false:取得失敗</returns>
	bool ReadKeyboard(Keyboard::State &states);

	/// <summary>
	/// コントローラーの入力状態を取得する
	/// </summary>
	/// <param name="state">取得結果の出力先。入力がない場合は未接続状態になる</param>
	/// <returns>true:取得成功、または入力なし false:取得失敗</returns>
	bool ReadGamepad(GamepadState &state);

	/// <summary>
	/// マウスの入力状態を取得する
	/// </summary>
	/// <param name="state">取得結果の出力先</param>
	/// <returns>true:取得成功、または入力なし false:取得失敗</returns>
	bool ReadMouse(MouseState &state);

	/// <summary>
	/// マウスの取得済みインターフェースと累積値の基準をクリアする
	/// </summary>
	void ResetMouseReading();

  private:
	HWND hwnd_ = nullptr;

	Microsoft::WRL::ComPtr<GameInput::v4::IGameInput> gameInput_;

	// 全キーボードの入力を集約する仮想デバイス
	APP_LOCAL_DEVICE_ID keyboardDeviceId_{};

	Microsoft::WRL::ComPtr<GameInput::v4::IGameInputDevice> keyboardDevice_;

	bool keyboardAggregateCreated_ = false;

	bool keyboardHadReading_ = false;

	// 全マウスの入力を集約する仮想デバイス
	APP_LOCAL_DEVICE_ID mouseDeviceId_{};

	Microsoft::WRL::ComPtr<GameInput::v4::IGameInputDevice> mouseDevice_;

	bool mouseAggregateCreated_ = false;
	bool mouseHadReading_ = false;

	GameInput::v4::GameInputMouseState previousMouseState_{};
	bool mouseReadingValid_ = false;
};
