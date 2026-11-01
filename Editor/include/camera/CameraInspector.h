#pragma once
#include "scene/SceneObjectId.h"
class ImGuiSystem;
class EditorHistory;
struct SceneObject;

/// <summary>
/// 描画用Cameraコンポーネントの投影とPrimary設定を編集する
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
	/// <returns>true:変更あり false:変更なし</returns>
	bool Draw(const ImGuiSystem &ui, SceneObjectId id, SceneObject &object,
	          EditorHistory &history) const;
};
