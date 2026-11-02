#pragma once

#include "scene/SceneObjectId.h"

#include "scene/MeshComponent.h"
#include <optional>

class Scene;

/// <summary>
/// 物体のMeshコンポーネントの有無と形状参照を変更前・変更後へ戻す操作
/// </summary>
class ModelPathCommand {
  public:
	/// <summary>
	/// Meshコンポーネントの変更内容を保持する。シーンへの反映は行わない
	/// </summary>
	/// <param name="objectId">編集する物体のID</param>
	/// <param name="before">変更前のMesh。nulloptはコンポーネントなし</param>
	/// <param name="after">変更後のMesh。nulloptはコンポーネントなし</param>
	ModelPathCommand(SceneObjectId objectId,
	                 std::optional<MeshComponent> before,
	                 std::optional<MeshComponent> after) noexcept;

	/// <summary>
	/// 変更前と変更後が異なるか確認する
	/// </summary>
	/// <returns>true:変更あり false:変更なし</returns>
	bool HasChanges() const;

	/// <summary>
	/// 変更前のMeshへ戻す
	/// </summary>
	/// <param name="scene">編集対象のシーン</param>
	/// <returns>true:成功 false:物体がない、または現在値が履歴と一致しない</returns>
	bool Undo(Scene &scene) const;

	/// <summary>
	/// 変更後のMeshを再適用する
	/// </summary>
	/// <param name="scene">編集対象のシーン</param>
	/// <returns>true:成功 false:物体がない、または現在値が履歴と一致しない</returns>
	bool Redo(Scene &scene) const;

  private:
	SceneObjectId objectId_ = 0;
	std::optional<MeshComponent> before_;
	std::optional<MeshComponent> after_;
};
