#pragma once

#include "scene/SceneObjectId.h"

class ImGuiSystem;
class EditorHistory;
struct SceneObject;

/// <summary>
/// モデルの元の材質へ掛ける色を、履歴付きで編集する
/// </summary>
class ColorTintInspector {
  public:
	/// <summary>
	/// 色がある物体にRGB・初期化・削除のUIを表示する。連続した色編集は1回の履歴にまとめる
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="id">表示対象の物体ID</param>
	/// <param name="object">編集する物体</param>
	/// <param name="history">変更を記録する履歴</param>
	/// <returns>true:有効な変更あり false:対象なしまたは変更なし</returns>
	bool Draw(const ImGuiSystem &ui, SceneObjectId id, SceneObject &object,
	          EditorHistory &history) const;
};
