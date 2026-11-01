#pragma once

#include "scene/SceneObject.h"
#include "scene/SceneObjectId.h"

class Scene;

/// <summary>
/// 物体の追加・削除と、その逆操作に必要なデータを保持する
/// </summary>
class ObjectCommand {
  public:
	/// <summary>
	/// 記録した操作の種類
	/// </summary>
	enum class Kind {
		CREATE,
		DELETE,
	};

	/// <summary>
	/// 物体の追加・削除内容を保持する。シーンへの反映は行わない
	/// </summary>
	/// <param name="objectId">操作した物体のID</param>
	/// <param name="object">操作した物体のデータ</param>
	/// <param name="kind">記録する操作の種類</param>
	ObjectCommand(SceneObjectId objectId, SceneObject object,
	              Kind kind) noexcept;

	/// <summary>
	/// 追加した物体を削除する、または削除した物体を復元する
	/// </summary>
	/// <param name="scene">編集対象のシーン</param>
	/// <returns>true:成功 false:シーンが履歴と一致しない</returns>
	bool Undo(Scene &scene) const;

	/// <summary>
	/// 記録した追加・削除を再実行する
	/// </summary>
	/// <param name="scene">編集対象のシーン</param>
	/// <returns>true:成功 false:シーンが履歴と一致しない</returns>
	bool Redo(Scene &scene) const;

  private:
	/// <summary>
	/// 現在の物体が記録したデータと一致する場合だけ削除する
	/// </summary>
	/// <param name="scene">編集対象のシーン</param>
	/// <returns>true:削除成功 false:物体がない、または内容が一致しない</returns>
	bool Remove(Scene &scene) const;

	SceneObjectId objectId_ = 0;
	SceneObject object_;
	Kind kind_ = Kind::CREATE;
};
