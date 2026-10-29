#pragma once

#include "input/InputState.h"

/// <summary>
/// OSごとの入力取得処理が実装するインターフェース
/// </summary>
class IInputBackend {
  public:
	virtual ~IInputBackend() = default;

	/// <summary>
	/// 入力取得に必要な処理を初期化する
	/// </summary>
	virtual bool Initialize() = 0;

	/// <summary>
	/// 最新の入力状態を取得する
	/// </summary>
	virtual bool Update(InputState &state) = 0;

	/// <summary>
	/// 入力取得に使用するリソースを解放する
	/// </summary>
	virtual void Finalize() = 0;
};
