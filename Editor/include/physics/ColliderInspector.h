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
	/// 描画後に実行する衝突形状の操作
	/// </summary>
	enum class Action { NONE, FIT_TO_MODEL, SELECT_OVERLAPS };

	/// <summary>
	/// 表示対象の物体へ適用する操作要求
	/// </summary>
	struct Request {
		Action action = Action::NONE;
		SceneObjectId objectId = 0;
	};

	/// <summary>
	/// 既存の衝突設定の削除と、中心・大きさの編集UIを描画する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="id">表示している物体のID</param>
	/// <param name="object">表示している物体</param>
	/// <param name="history">編集を記録する履歴</param>
	/// <param name="request">操作要求。操作がなければNONEへ戻す。検索と形状合わせは描画後に行う</param>
	/// <returns>true:設定を編集した false:設定への変更なし。操作要求だけでは編集扱いにしない</returns>
	bool Draw(const ImGuiSystem &ui, SceneObjectId id, SceneObject &object,
	          EditorHistory &history, Request &request) const;
};
