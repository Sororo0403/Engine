#pragma once

#include "physics/Collider.h"
#include "scene/SceneObjectId.h"

#include <optional>

class Scene;

/// <summary>
/// 衝突用ボックスの追加・編集・削除を元に戻し、再実行する
/// </summary>
class ColliderCommand {
  public:
	/// <summary>
	/// 編集前後の衝突設定を保持する
	/// </summary>
	/// <param name="objectId">対象物体のID</param>
	/// <param name="before">変更前。未指定はコンポーネントなし</param>
	/// <param name="after">変更後。未指定はコンポーネントなし</param>
	ColliderCommand(SceneObjectId objectId, std::optional<Collider> before,
	                std::optional<Collider> after);

	/// <summary>
	/// 設定が変わる操作か確認する
	/// </summary>
	/// <returns>true:変更あり false:変更なし</returns>
	bool HasChanges() const {
		return before_ != after_;
	}

	/// <summary>
	/// 衝突設定を元に戻す
	/// </summary>
	/// <param name="scene">編集対象のシーン</param>
	/// <returns>true:成功 false:物体や設定が一致しない、または復元先が無効</returns>
	bool Undo(Scene &scene) const;

	/// <summary>
	/// 衝突設定の変更を再実行する
	/// </summary>
	/// <param name="scene">編集対象のシーン</param>
	/// <returns>true:成功 false:物体や設定が一致しない、または変更先が無効</returns>
	bool Redo(Scene &scene) const;

  private:
	SceneObjectId objectId_;
	std::optional<Collider> before_;
	std::optional<Collider> after_;
};
