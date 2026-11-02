#pragma once

#include "scene/SceneObjectId.h"
class ImGuiSystem;
class EditorHistory;
class ModelManager;
class AssetDatabase;
struct SceneObject;

/// <summary>
/// モデルの材質スロットごとに、独立した材質アセットを割り当てるUI
/// </summary>
class MaterialSlotInspector {
  public:
	/// <summary>
	/// 既定材質への復帰とアセットの割り当てを表示し、履歴へ記録する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="id">表示中の物体</param>
	/// <param name="object">変更する物体データ</param>
	/// <param name="history">割り当ての編集履歴</param>
	/// <param name="models">材質スロット数を取得するモデル管理</param>
	/// <param name="assets">割り当て可能な材質の一覧</param>
	/// <returns>true:割り当て変更あり false:変更なし</returns>
	bool Draw(const ImGuiSystem &ui, SceneObjectId id, SceneObject &object,
	          EditorHistory &history, ModelManager &models,
	          const AssetDatabase &assets) const;
};
