#pragma once

#include "gameplay/FollowTargetPicker.h"
#include "scene/SceneObjectId.h"

class ImGuiSystem;
class Scene;
class EditorHistory;
struct SceneObject;

/// <summary>
/// 移動・追従カメラ・落下復帰の設定をInspectorで編集する
/// </summary>
class GameplayInspector {
  public:
	/// <summary>
	/// 存在するコンポーネントを表示し、編集を履歴へ記録する
	/// </summary>
	/// <param name="ui">描画用UI</param>
	/// <param name="scene">追従対象を選ぶシーン</param>
	/// <param name="id">表示対象ID</param>
	/// <param name="object">編集する物体</param>
	/// <param name="history">編集履歴</param>
	/// <returns>true:変更あり false:変更なし</returns>
	bool Draw(const ImGuiSystem &ui, const Scene &scene, SceneObjectId id,
	          SceneObject &object, EditorHistory &history);
	/// <summary>
	/// コンポーネント選択UIの検索状態を解除する
	/// </summary>
	void Clear() noexcept;

  private:
	FollowTargetPicker targetPicker_;
};
