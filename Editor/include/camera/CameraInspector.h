#pragma once

#include "scene/SceneObjectId.h"

class ImGuiSystem;
class EditorHistory;
struct SceneObject;

/// <summary>
/// 描画用Cameraの設定編集と、描画後に処理する操作要求を管理する
/// </summary>
class CameraInspector {
  public:
	/// <summary>
	/// Inspector描画後に適用するCameraの操作
	/// </summary>
	enum class Action {
		NONE,
		ALIGN_VIEW,
		MAKE_PRIMARY,
	};

	/// <summary>
	/// 描画中の物体参照を使い終えてから処理する操作と対象ID
	/// </summary>
	struct Request {
		Action action = Action::NONE;
		SceneObjectId objectId = 0;
	};

	/// <summary>
	/// カメラ設定を表示し、レンズの初期化・編集と削除を履歴へ記録する
	/// </summary>
	/// <param name="ui">描画用UI</param>
	/// <param name="id">対象物体のID</param>
	/// <param name="object">編集する物体</param>
	/// <param name="history">編集履歴</param>
	/// <param name="request">姿勢合わせまたはPrimary切り替えの要求。操作がなければ呼び出し前の値を保つ</param>
	/// <returns>true:設定変更あり false:変更なし、または操作要求のみ</returns>
	bool Draw(const ImGuiSystem &ui, SceneObjectId id, SceneObject &object,
	          EditorHistory &history, Request &request) const;
};
