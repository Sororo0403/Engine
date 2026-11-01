#pragma once

#include "scene/SceneObjectId.h"

#include <cstddef>
#include <span>
#include <vector>

class Scene;

/// <summary>
/// Editorの選択物体と、最後に選択したアクティブ物体を管理する
/// </summary>
class EditorSelection {
  public:
	/// <summary>
	/// 指定した物体だけを選択する
	/// </summary>
	/// <param name="objectId">選択する物体ID。0の場合は全選択を解除する</param>
	/// <returns>true:選択変更あり false:変更なし</returns>
	bool SelectOnly(SceneObjectId objectId);

	/// <summary>
	/// 物体の選択状態を切り替える
	/// </summary>
	/// <param name="objectId">対象物体のID。0の場合は何もしない</param>
	/// <returns>true:選択変更あり false:変更なし</returns>
	bool Toggle(SceneObjectId objectId);

	/// <summary>
	/// 指定した物体を選択から取り除く
	/// </summary>
	/// <param name="objectId">対象物体のID</param>
	/// <returns>true:選択変更あり false:対象は未選択</returns>
	bool Remove(SceneObjectId objectId);

	/// <summary>
	/// シーンに存在しない物体を選択から取り除く
	/// </summary>
	/// <param name="scene">選択対象のシーン</param>
	/// <returns>true:選択変更あり false:変更なし</returns>
	bool RemoveMissing(const Scene &scene);

	/// <summary>
	/// 全ての選択を解除する
	/// </summary>
	/// <returns>true:選択変更あり false:選択なし</returns>
	bool Clear();

	/// <summary>
	/// 指定した物体が選択されているか確認する
	/// </summary>
	/// <param name="objectId">確認する物体ID</param>
	/// <returns>true:選択中 false:未選択</returns>
	bool Contains(SceneObjectId objectId) const;

	/// <summary>
	/// 選択物体数を取得する
	/// </summary>
	/// <returns>選択物体数</returns>
	std::size_t GetCount() const;

	/// <summary>
	/// 最後に選択した、現在も選択中の物体IDを取得する
	/// </summary>
	/// <returns>アクティブ物体のID。選択なしの場合は0</returns>
	SceneObjectId GetActiveId() const;

	/// <summary>
	/// 選択した順の物体ID一覧を取得する
	/// </summary>
	/// <returns>次の選択変更まで有効な読み取り専用の一覧</returns>
	std::span<const SceneObjectId> GetIds() const;

  private:
	// 重複と0を含まず、末尾をアクティブ物体として扱う
	std::vector<SceneObjectId> ids_;
};
