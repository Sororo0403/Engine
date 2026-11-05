#pragma once

#include "scene/SceneObjectId.h"

class ImGuiSystem;
class EditorHistory;
struct SceneObject;

/// <summary>
/// 点光源の色・強さ・範囲を履歴付きで編集する
/// </summary>
class PointLightInspector {
  public:
	/// <summary>
	/// 点光源の編集・初期化・削除を表示する。連続した操作は1回の履歴にまとめる
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="id">表示対象の物体ID</param>
	/// <param name="object">編集する物体</param>
	/// <param name="history">変更を記録する履歴</param>
	/// <returns>true:有効な変更あり false:対象なしまたは変更なし</returns>
	bool Draw(const ImGuiSystem &ui, SceneObjectId id, SceneObject &object,
	          EditorHistory &history) const;

	/// <summary>
	/// Play中の点光源設定を変更せず表示する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="object">表示する再生用物体</param>
	void DrawReadOnly(const ImGuiSystem &ui, const SceneObject &object) const;
};
