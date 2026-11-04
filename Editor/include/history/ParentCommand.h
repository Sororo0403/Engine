#pragma once

#include "math/Transform.h"
#include "scene/SceneObjectId.h"

class Scene;

/// <summary>
/// 1つの物体の親とローカル変形をまとめて変更前・変更後へ戻す操作
/// </summary>
class ParentCommand {
  public:
	/// <summary>
	/// 親とローカル変形の変更内容を保持する。シーンへの反映は行わない
	/// </summary>
	/// <param name="id">変更する物体ID</param>
	/// <param name="beforeParent">変更前の親。0はルート</param>
	/// <param name="afterParent">変更後の親。0はルート</param>
	/// <param name="before">変更前のローカル変形</param>
	/// <param name="after">変更後のローカル変形</param>
	ParentCommand(SceneObjectId id, SceneObjectId beforeParent,
	              SceneObjectId afterParent, const Transform &before,
	              const Transform &after) noexcept;

	/// <summary>
	/// 親とローカル変形を変更前へ戻す
	/// </summary>
	/// <param name="scene">編集対象のシーン</param>
	/// <returns>true:成功 false:現在値が履歴と異なる、または親子関係や変形が無効</returns>
	bool Undo(Scene &scene) const;

	/// <summary>
	/// 親とローカル変形を変更後へ戻す
	/// </summary>
	/// <param name="scene">編集対象のシーン</param>
	/// <returns>true:成功 false:現在値が履歴と異なる、または親子関係や変形が無効</returns>
	bool Redo(Scene &scene) const;

  private:
	SceneObjectId objectId_ = 0;
	SceneObjectId beforeParent_ = 0, afterParent_ = 0;
	Transform before_, after_;
};
