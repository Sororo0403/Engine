#pragma once

#include "math/Transform.h"
#include "scene/SceneObjectId.h"

class Scene;

/// <summary>
/// 1つの物体の変形情報を変更前・変更後へ戻す操作
/// </summary>
class TransformCommand {
  public:
	/// <summary>
	/// 変形情報の変更内容を保持する。シーンへの反映は行わない
	/// </summary>
	/// <param name="objectId">編集した物体のID</param>
	/// <param name="before">変更前の変形情報</param>
	/// <param name="after">変更後の変形情報</param>
	TransformCommand(SceneObjectId objectId, const Transform &before,
	                 const Transform &after) noexcept;

	/// <summary>
	/// 変更前と変更後が異なるか確認する
	/// </summary>
	/// <returns>true:変更あり false:変更なし</returns>
	bool HasChanges() const;

	/// <summary>
	/// 物体を変更前の変形情報へ戻す
	/// </summary>
	/// <param name="scene">編集対象のシーン</param>
	/// <returns>true:成功 false:物体がない、または現在値が履歴と一致しない</returns>
	bool Undo(Scene &scene) const;

	/// <summary>
	/// 物体へ変更後の変形情報を再適用する
	/// </summary>
	/// <param name="scene">編集対象のシーン</param>
	/// <returns>true:成功 false:物体がない、または現在値が履歴と一致しない</returns>
	bool Redo(Scene &scene) const;

  private:
	SceneObjectId objectId_ = 0;

	Transform before_;
	Transform after_;
};
