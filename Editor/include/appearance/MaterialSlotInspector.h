#pragma once

#include "scene/SceneObjectId.h"
#include <filesystem>
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
	/// 描画後に材質編集パネルで開くアセットへの要求
	/// </summary>
	struct Request {
		std::filesystem::path editMaterial;
	};

	/// <summary>
	/// 既定材質への復帰とアセットの割り当てを表示し、履歴へ記録する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="id">表示中の物体</param>
	/// <param name="object">変更する物体データ</param>
	/// <param name="history">割り当ての編集履歴</param>
	/// <param name="models">材質スロット数を取得するモデル管理</param>
	/// <param name="assets">割り当て可能な材質の一覧</param>
	/// <param name="request">材質を開く要求。操作なしの場合はクリアする</param>
	/// <returns>true:割り当て変更あり false:変更なし、または編集要求のみ</returns>
	bool Draw(const ImGuiSystem &ui, SceneObjectId id, SceneObject &object,
	          EditorHistory &history, ModelManager &models,
	          const AssetDatabase &assets, Request &request) const;
};
