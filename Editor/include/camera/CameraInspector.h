#pragma once

#include "scene/SceneObjectId.h"

class ImGuiSystem;
class EditorHistory;
struct SceneObject;

/// <summary>
/// 描画用Cameraの設定編集と、Sceneビューへの姿勢合わせの要求を管理する
/// </summary>
class CameraInspector {
  public:
	/// <summary>
	/// カメラ設定を表示し、編集と削除を履歴へ記録する
	/// </summary>
	/// <param name="ui">描画用UI</param>
	/// <param name="id">対象物体のID</param>
	/// <param name="object">編集する物体</param>
	/// <param name="history">編集履歴</param>
	/// <param name="alignRequest">Sceneビューへの姿勢合わせを要求したID。要求なしの場合は呼び出し前の値を保つ</param>
	/// <returns>true:設定変更あり false:変更なし、または姿勢合わせの要求のみ</returns>
	bool Draw(const ImGuiSystem &ui, SceneObjectId id, SceneObject &object,
	          EditorHistory &history, SceneObjectId &alignRequest) const;
};
