#pragma once

#include "scene/SceneObjectId.h"

#include <filesystem>

class Scene;

/// <summary>
/// 物体のモデル参照を変更前・変更後へ戻す操作
/// </summary>
class ModelPathCommand {
  public:
	/// <summary>
	/// モデル参照の変更内容を保持する。シーンへの反映は行わない
	/// </summary>
	/// <param name="objectId">編集する物体のID</param>
	/// <param name="before">変更前のモデル。空の場合はモデルなし</param>
	/// <param name="after">変更後のモデル。空の場合はモデルなし</param>
	ModelPathCommand(SceneObjectId objectId, std::filesystem::path before,
	                 std::filesystem::path after) noexcept;

	/// <summary>
	/// 変更前と変更後が異なるか確認する
	/// </summary>
	/// <returns>true:変更あり false:変更なし</returns>
	bool HasChanges() const;

	/// <summary>
	/// 変更前のモデル参照へ戻す
	/// </summary>
	/// <param name="scene">編集対象のシーン</param>
	/// <returns>true:成功 false:物体がない、または現在値が履歴と一致しない</returns>
	bool Undo(Scene &scene) const;

	/// <summary>
	/// 変更後のモデル参照を再適用する
	/// </summary>
	/// <param name="scene">編集対象のシーン</param>
	/// <returns>true:成功 false:物体がない、または現在値が履歴と一致しない</returns>
	bool Redo(Scene &scene) const;

  private:
	SceneObjectId objectId_ = 0;
	std::filesystem::path before_;
	std::filesystem::path after_;
};
