#pragma once

#include "scene/SceneObjectId.h"

class Scene;

/// <summary>
/// 物体自身の有効設定を元に戻し、再実行する。子自身の設定は変更しない
/// </summary>
class ActiveCommand {
  public:
	/// <summary>
	/// 編集対象と変更前後の設定を保持する
	/// </summary>
	/// <param name="objectId">対象のID</param>
	/// <param name="before">変更前の設定</param>
	/// <param name="after">変更後の設定</param>
	ActiveCommand(SceneObjectId objectId, bool before, bool after) noexcept;

	/// <summary>
	/// 元の設定へ戻す
	/// </summary>
	/// <param name="scene">編集するシーン</param>
	/// <returns>true:成功 false:物体が存在しない、または現在値が一致しない</returns>
	bool Undo(Scene &scene) const;

	/// <summary>
	/// 変更後の設定へ戻す
	/// </summary>
	/// <param name="scene">編集するシーン</param>
	/// <returns>true:成功 false:物体が存在しない、または現在値が一致しない</returns>
	bool Redo(Scene &scene) const;

  private:
	SceneObjectId objectId_;
	bool before_;
	bool after_;
};
