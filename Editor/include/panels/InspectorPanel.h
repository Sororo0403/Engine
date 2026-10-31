#pragma once

#include "scene/SceneObjectId.h"

class ImGuiSystem;
class Scene;

/// <summary>
/// 選択した物体の情報表示と変形情報の編集を管理する
/// </summary>
class InspectorPanel {
  public:
	/// <summary>
	/// 選択した物体の編集用UIを構築する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">編集対象のシーン</param>
	/// <param name="selectedObjectId">選択中の物体ID。未選択は0</param>
	void Draw(const ImGuiSystem &ui, Scene &scene,
	          SceneObjectId selectedObjectId) const;
};
