#pragma once
#include "scene/SceneObjectId.h"
class ImGuiSystem;
class EditorHistory;
struct SceneObject;

/// <summary>
/// 変形のキーと再生設定を編集し、連続した操作を1回のUndoへまとめる
/// </summary>
class TransformAnimationInspector {
  public:
	/// <summary>
	/// 物体の相対キーフレームと再生設定を表示・編集する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="id">編集する物体</param>
	/// <param name="object">履歴が参照する編集シーンの物体</param>
	/// <param name="history">同じシーンのUndo履歴</param>
	/// <returns>true:編集データを変更 false:変更なし</returns>
	bool Draw(const ImGuiSystem &ui, SceneObjectId id, SceneObject &object,
	          EditorHistory &history) const;

	/// <summary>
	/// 再生中のコンポーネント設定を、履歴を変更せず表示する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="object">再生用シーンの物体</param>
	void DrawReadOnly(const ImGuiSystem &ui, const SceneObject &object) const;
};
