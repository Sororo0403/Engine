#pragma once

#include "scene/SceneObjectId.h"
class ImGuiSystem;
class EditorHistory;
struct SceneObject;

/// <summary>
/// 経路移動の点・区間時間・再生方式を、連続編集とUndo付きで設定する
/// </summary>
class MotionPathInspector {
  public:
	/// <summary>
	/// 経路の設定・点の追加削除・コンポーネントの初期化と削除を表示する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="id">編集する物体</param>
	/// <param name="object">経路を持つ物体</param>
	/// <param name="history">変更を記録する履歴</param>
	/// <returns>true:有効な変更あり false:経路なしまたは変更なし</returns>
	bool Draw(const ImGuiSystem &ui, SceneObjectId id, SceneObject &object,
	          EditorHistory &history) const;

	/// <summary>
	/// 再生中の経路設定を編集せず表示する
	/// </summary>
	/// <param name="ui">表示するUI</param>
	/// <param name="object">再生用物体</param>
	void DrawReadOnly(const ImGuiSystem &ui, const SceneObject &object) const;
};
