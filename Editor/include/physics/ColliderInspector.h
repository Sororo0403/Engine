#pragma once

#include "scene/SceneObjectId.h"

class EditorHistory;
class ImGuiSystem;
class Scene;
struct SceneObject;

/// <summary>
/// Inspector内の箱・球・カプセルの編集UIを担当する
/// </summary>
class ColliderInspector {
  public:
	/// <summary>
	/// 描画後に実行する衝突形状の操作
	/// </summary>
	enum class Action {
		NONE,
		FIT_TO_MODEL,
		SELECT_OVERLAPS,
		FRAME,
		COPY,
		PASTE
	};

	/// <summary>
	/// 表示対象の物体へ適用する操作要求
	/// </summary>
	struct Request {
		Action action = Action::NONE;
		SceneObjectId objectId = 0;
	};

	/// <summary>
	/// 既存の衝突設定の削除と、形状・中心・寸法・衝突レイヤー・Triggerの編集UIを描画する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">表示物体と祖先のワールド変形を取得するシーン</param>
	/// <param name="id">表示している物体のID</param>
	/// <param name="object">表示している物体</param>
	/// <param name="history">編集を記録する履歴</param>
	/// <param name="request">操作要求。操作がなければNONEへ戻す。検索・形状合わせ・カメラ移動・作業平面の設定・コピー・貼り付けは描画後に行う</param>
	/// <param name="canPasteCollider">貼り付けるColliderのコピーを保持しているか</param>
	/// <returns>true:設定を編集した false:設定への変更なし。操作要求だけでは編集扱いにしない</returns>
	bool Draw(const ImGuiSystem &ui, const Scene &scene, SceneObjectId id,
	          SceneObject &object, EditorHistory &history, Request &request,
	          bool canPasteCollider = false) const;
};
