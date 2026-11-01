#pragma once

#include "scene/SceneObjectId.h"

class ImGuiSystem;
class Scene;

/// <summary>
/// シーン上の物体の一覧・選択・操作要求を管理する
/// </summary>
class HierarchyPanel {
  public:
	/// <summary>
	/// Hierarchyから要求する操作
	/// </summary>
	enum class Action {
		NONE,
		CREATE_EMPTY,
		DELETE_OBJECT,
	};

	/// <summary>
	/// Hierarchyの操作要求と入力状態
	/// </summary>
	struct Result {
		Action action = Action::NONE;
		SceneObjectId objectId = 0;

		bool windowFocused = false;
	};

	/// <summary>
	/// 物体の一覧と操作用UIを構築する。
	/// 物体の追加・削除は実行せず、要求として返す
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">表示対象のシーン</param>
	/// <param name="selectedObjectId">選択中の物体ID。未選択は0</param>
	/// <returns>要求された操作</returns>
	Result Draw(const ImGuiSystem &ui, const Scene &scene,
	            SceneObjectId &selectedObjectId) const;
};
