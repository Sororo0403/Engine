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
	explicit WindowsInputBackend(HWND hwnd);

	~WindowsInputBackend() override;

	bool Initialize() override;
	bool Update(InputState &state) override;
	void Finalize() override;

  private:
	/// <summary>
	/// Windowsの仮想キーコードを共通のキーに変換する
	/// </summary>
	static Key ToKey(unsigned int virtualKey);

	HWND hwnd_ = nullptr;

	Microsoft::WRL::ComPtr<GameInput::v4::IGameInput> gameInput_;
};
