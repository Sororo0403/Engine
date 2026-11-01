#pragma once

#include "scene/SceneObjectId.h"

class Scene;

/// <summary>
/// 物体のモデルの表示状態を変更前・変更後へ戻す操作
/// </summary>
class VisibilityCommand {
  public:
	/// <summary>
	/// 表示状態の変更内容を保持する。シーンへの反映は行わない
	/// </summary>
	/// <param name="objectId">対象物体のID</param>
	/// <param name="before">変更前の表示状態</param>
	/// <param name="after">変更後の表示状態</param>
	VisibilityCommand(SceneObjectId objectId, bool before, bool after) noexcept;

	/// <summary>
	/// 変更前の表示状態へ戻す
	/// </summary>
	/// <param name="scene">編集対象のシーン</param>
	/// <returns>true:成功 false:物体がない、または現在値が履歴と一致しない</returns>
	bool Undo(Scene &scene) const;

	/// <summary>
	/// 変更後の表示状態を再適用する
	/// </summary>
	/// <param name="scene">編集対象のシーン</param>
	/// <returns>true:成功 false:物体がない、または現在値が履歴と一致しない</returns>
	bool Redo(Scene &scene) const;

  private:
	SceneObjectId objectId_ = 0;
	bool before_ = true;
	bool after_ = true;
};
