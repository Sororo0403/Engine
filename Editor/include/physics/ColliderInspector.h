#pragma once

#include "scene/SceneObjectId.h"

class EditorHistory;
class ImGuiSystem;
struct SceneObject;

/// <summary>
/// Inspector内の衝突用ボックスの編集UIを担当する
/// </summary>
class ColliderInspector {
  public:
	/// <summary>
	/// 既存の衝突設定の削除と、中心・大きさの編集UIを描画する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="id">表示している物体のID</param>
	/// <param name="object">表示している物体</param>
	/// <param name="history">編集を記録する履歴</param>
	/// <param name="fitRequest">モデルへの形状合わせを要求した場合に設定するID</param>
	/// <returns>true:設定を編集した false:設定への変更なし</returns>
	bool Draw(const ImGuiSystem &ui, SceneObjectId id, SceneObject &object,
	          EditorHistory &history, SceneObjectId &fitRequest) const;
};
