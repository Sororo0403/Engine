#pragma once

#include "scene/SceneObjectId.h"

class ImGuiSystem;
class Scene;

/// <summary>
/// シーン上の物体の一覧・選択・作成・削除を管理する
/// </summary>
class HierarchyPanel {
  public:
	/// <summary>
	/// 物体の一覧と操作用UIを構築する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">編集対象のシーン</param>
	/// <param name="selectedObjectId">選択中の物体ID。未選択は0</param>
	/// <returns>true:物体を作成・削除した false:シーンへの変更なし</returns>
	bool Draw(const ImGuiSystem &ui, Scene &scene,
	          SceneObjectId &selectedObjectId) const;
};
