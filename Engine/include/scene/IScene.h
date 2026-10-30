#pragma once

/// <summary>
/// シーン共通のライフサイクルを定義する
/// </summary>
class IScene {
  public:
	virtual ~IScene() = default;

	/// <summary>
	/// シーンを初期化する
	/// </summary>
	/// <returns>true:成功 false:初期化失敗</returns>
	virtual bool Initialize() = 0;

	/// <summary>
	/// シーンの状態を更新する
	/// </summary>
	/// <param name="deltaTime">前フレームからの経過時間。単位は秒</param>
	/// <returns>true:成功 false:更新失敗</returns>
	virtual bool Update(float deltaTime) = 0;

	/// <summary>
	/// シーンを描画する
	/// </summary>
	virtual void Draw() const = 0;

	/// <summary>
	/// シーンのリソースを解放する。GPUの利用完了後に呼び出す
	/// </summary>
	virtual void Finalize() = 0;
};
