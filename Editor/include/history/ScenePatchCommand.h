#pragma once
#include "scene/SceneAssetReferences.h"

#include "scene/SceneObject.h"
#include <optional>
#include <utility>
#include <vector>
class Scene;

/// <summary>
/// 追加・変更・削除した物体だけを保持し、関連する階層と参照を一括して戻す
/// </summary>
class ScenePatchCommand {
  public:
	/// <summary>
	/// 変更前後の差分を保持する。シーンには反映しない
	/// </summary>
	/// <param name="before">変更前のシーン</param>
	/// <param name="after">検証済みの変更後のシーン</param>
	ScenePatchCommand(const Scene &before, const Scene &after);

	/// <summary>
	/// 差分があるか確認する
	/// </summary>
	/// <returns>true:差分あり false:同値</returns>
	bool HasChanges() const;

	/// <summary>
	/// 想定する物体の状態を確認し、変更前へまとめて戻す
	/// </summary>
	/// <param name="scene">編集対象</param>
	/// <returns>true:成功 false:想定値・階層・データが不一致。失敗時は全物体を保持する</returns>
	bool Undo(Scene &scene) const;

	/// <summary>
	/// 変更後の差分をまとめて適用する
	/// </summary>
	/// <param name="scene">編集対象</param>
	/// <returns>true:成功 false:想定値・階層・データが不一致。失敗時は全物体を保持する</returns>
	bool Redo(Scene &scene) const;

	/// <summary>
	/// 履歴に保持する値の素材参照を更新する
	/// </summary>
	void RewriteObjects(const SceneAssetReferences::ObjectRewrite &rewrite) {
		for (auto *values : {&before_, &after_}) {
			for (auto &[id, object] : *values) {
				(void)id;
				if (object) {
					rewrite(*object);
				}
			}
		}
	}

  private:
	using Values =
	    std::vector<std::pair<SceneObjectId, std::optional<SceneObject>>>;
	/// <summary>
	/// 変更対象の想定値を確認し、検証済みの候補だけを確定する
	/// </summary>
	/// <param name="scene">編集対象</param>
	/// <param name="expected">想定する現在値</param>
	/// <param name="values">変更後の値。nulloptは削除</param>
	/// <returns>true:確定成功 false:不一致または検証失敗</returns>
	bool Apply(Scene &scene, const Values &expected,
	           const Values &values) const;
	Values before_, after_;
};
