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
	/// <returns>true:成功 false:失敗</returns>
	virtual bool Initialize() = 0;

	/// <summary>
	/// 最新の入力状態を取得する
	/// </summary>
	/// <param name="state">取得結果の出力先。入力を受け付けない場合はactiveをfalseにする</param>
	/// <returns>true:更新成功（入力を受け付けない場合を含む） false:更新失敗</returns>
	virtual bool Update(InputState &state) = 0;

	/// <summary>
	/// 入力取得に使用するリソースを解放する
	/// </summary>
	virtual void Finalize() = 0;
};
