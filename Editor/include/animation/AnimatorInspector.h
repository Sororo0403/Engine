#pragma once
#include "scene/SceneObjectId.h"
class ImGuiSystem;
class EditorHistory;
class ModelManager;
struct SceneObject;

/// <summary>
/// モデル内のクリップ・速度・ループ・自動再生を編集し、履歴へまとめる
/// </summary>
class AnimatorInspector {
  public:
	/// <summary>
	/// Animatorを表示し、モデルの共有データから既定クリップを選ぶ
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="id">編集する物体</param>
	/// <param name="object">履歴と同じ編集シーンの物体</param>
	/// <param name="history">連続編集・追加・削除の履歴</param>
	/// <param name="models">モデルの所有側。指定時は一覧を開いた時に未読のクリップを準備する</param>
	/// <returns>true:変更あり false:変更なし</returns>
	bool Draw(const ImGuiSystem &ui, SceneObjectId id, SceneObject &object,
	          EditorHistory &history, ModelManager *models = nullptr) const;

	/// <summary>
	/// 再生中のシーンに保存されている初期設定を、履歴を変更せず表示する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="object">再生物体の初期設定</param>
	void DrawReadOnly(const ImGuiSystem &ui, const SceneObject &object) const;
};
