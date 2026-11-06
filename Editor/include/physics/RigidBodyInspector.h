#pragma once
#include "scene/SceneObjectId.h"
class ImGuiSystem;
class EditorHistory;
struct SceneObject;

/// <summary>
/// 剛体の質量・摩擦・速度と回転固定を編集する
/// </summary>
class RigidBodyInspector {
  public:
	/// <summary>
	/// 剛体設定を表示し、連続した操作を1回のUndoへまとめる
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="id">編集対象の物体ID</param>
	/// <param name="object">変更するコンポーネント</param>
	/// <param name="history">編集の履歴</param>
	/// <returns>true:設定を変更 false:変更なし</returns>
	bool Draw(const ImGuiSystem &ui, SceneObjectId id, SceneObject &object,
	          EditorHistory &history) const;

	/// <summary>
	/// 再生中の剛体設定を変更できない形で表示する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="object">再生中の物体</param>
	void DrawReadOnly(const ImGuiSystem &ui, const SceneObject &object) const;
};
