#pragma once

#include "scene/SceneObjectId.h"
#include <filesystem>
#include <span>

class ImGuiSystem;
class EditorHistory;
class Scene;
struct SceneObject;

/// <summary>
/// Inspector内でTrigger Actionsの順序・条件・対象を編集する
/// </summary>
class TriggerActionsInspector {
  public:
	/// <summary>
	/// 設定済みコンポーネントを表示し、各操作をUndo／Redoへ記録する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">参照先の候補を取得するシーン</param>
	/// <param name="id">編集対象の物体ID</param>
	/// <param name="object">編集対象の物体</param>
	/// <param name="history">編集履歴</param>
	/// <param name="scenes">Assets内の遷移先候補。未指定でもシーンをドロップできる</param>
	/// <returns>true:設定を変更した false:変更なし</returns>
	bool Draw(const ImGuiSystem &ui, const Scene &scene, SceneObjectId id,
	          SceneObject &object, EditorHistory &history,
	          std::span<const std::filesystem::path> scenes = {}) const;

	/// <summary>
	/// 再生中の物体の設定を変更せずに表示する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">対象と参照先の名前を読むシーン</param>
	/// <param name="object">表示する物体</param>
	void DrawReadOnly(const ImGuiSystem &ui, const Scene &scene,
	                  const SceneObject &object) const;
};
