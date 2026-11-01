#pragma once

#include "scene/SceneObjectId.h"

#include <string>

class Scene;

/// <summary>
/// 1つの物体の名前を変更前・変更後へ戻す操作
/// </summary>
class RenameCommand {
  public:
	/// <summary>
	/// 名前の変更内容を保持する。シーンへの反映は行わない
	/// </summary>
	/// <param name="objectId">編集した物体のID</param>
	/// <param name="before">変更前の名前</param>
	/// <param name="after">変更後の名前</param>
	RenameCommand(SceneObjectId objectId, std::string before,
	              std::string after) noexcept;

	/// <summary>
	/// 変更前と変更後が異なるか確認する
	/// </summary>
	/// <returns>true:変更あり false:変更なし</returns>
	bool HasChanges() const;

	/// <summary>
	/// 物体を変更前の名前へ戻す
	/// </summary>
	/// <param name="scene">編集対象のシーン</param>
	/// <returns>true:成功 false:物体がない、または現在値が履歴と一致しない</returns>
	bool Undo(Scene &scene) const;

	/// <summary>
	/// 物体へ変更後の名前を再適用する
	/// </summary>
	/// <param name="scene">編集対象のシーン</param>
	/// <returns>true:成功 false:物体がない、または現在値が履歴と一致しない</returns>
	bool Redo(Scene &scene) const;

  private:
	SceneObjectId objectId_ = 0;

	std::string before_;
	std::string after_;
};
