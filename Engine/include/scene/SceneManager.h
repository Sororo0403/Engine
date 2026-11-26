#pragma once

#include "scene/IScene.h"

#include <memory>
#include <string>

/// <summary>
/// 現在のシーンと、シーン切り替えの予約を管理する
/// </summary>
class SceneManager {
  public:
	SceneManager() = default;

	SceneManager(const SceneManager &) = delete;
	SceneManager &operator=(const SceneManager &) = delete;

	/// <summary>
	/// 次のシーンへの切り替えを予約する
	/// </summary>
	/// <param name="nextScene">未初期化のシーン。所有権をこのクラスへ渡す</param>
	/// <returns>true:予約成功 false:シーンが無効、または予約済み</returns>
	bool RequestChange(std::unique_ptr<IScene> nextScene);

	/// <summary>
	/// 予約されたシーンへ切り替える。GPUの利用完了後、フレーム開始前に呼び出す
	/// </summary>
	/// <returns>true:切り替え成功または予約なし false:次のシーンの初期化失敗</returns>
	bool ApplyPendingChange();

	/// <summary>
	/// 現在のシーンを更新する
	/// </summary>
	/// <param name="deltaTime">前フレームからの経過時間。単位は秒</param>
	/// <returns>true:成功 false:シーンなし、時間が無効、または更新失敗</returns>
	bool Update(float deltaTime);

	/// <summary>
	/// 現在のシーンを描画する
	/// </summary>
	void Draw() const;

	/// <summary>
	/// 現在のシーンと切り替え予約を解放する。GPUの利用完了後に呼び出す
	/// </summary>
	void Finalize();

	/// <summary>
	/// 現在のシーンへ終了要求を渡す
	/// </summary>
	/// <returns>true:直ちに終了可能 false:終了を保留または取り消す</returns>
	bool RequestClose();

	/// <summary>
	/// 保留していた終了が許可されたか取得する
	/// </summary>
	/// <returns>true:終了可能 false:終了しない</returns>
	bool IsCloseApproved() const;

	/// <summary>
	/// 現在のシーンが指定するウィンドウタイトルを取得する
	/// </summary>
	/// <returns>UTF-8のタイトル。指定がない場合は空文字列</returns>
	std::string GetWindowTitle() const;

  private:
	std::unique_ptr<IScene> currentScene_;
	std::unique_ptr<IScene> pendingScene_;
};
