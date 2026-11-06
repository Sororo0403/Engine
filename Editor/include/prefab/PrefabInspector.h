#pragma once

#include "scene/SceneObjectId.h"
class Scene;
class ImGuiSystem;

/// <summary>
/// Prefabの参照先を表示し、描画後に行う更新・保存・解除を要求する
/// </summary>
class PrefabInspector {
  public:
	/// <summary>
	/// Inspectorから要求する操作
	/// </summary>
	enum class Action {
		NONE,
		REFRESH,
		REFRESH_ALL,
		REVERT,
		SAVE_ASSET,
		UNPACK,
		SELECT_ROOT,
		REVEAL_ASSET
	};

	/// <summary>
	/// 対象の根と、描画完了後に処理する操作
	/// </summary>
	struct Request {
		Action action = Action::NONE;
		SceneObjectId root = 0;
	};

	/// <summary>
	/// 根または子のリンク情報と操作を表示する。描画中にはシーンもファイルも変更しない
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">表示するシーン</param>
	/// <param name="member">表示中の物体</param>
	/// <param name="editable">編集操作を許可するか</param>
	/// <returns>処理する要求。リンクなし、または操作なしの場合はNONE</returns>
	Request Draw(const ImGuiSystem &ui, const Scene &scene,
	             SceneObjectId member, bool editable = true) const;
};
