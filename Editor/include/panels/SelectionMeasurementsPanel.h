#pragma once

#include "scene/SceneObjectId.h"
#include <span>

class ImGuiSystem;
class Scene;

/// <summary>
/// 選択した物体の原点間距離とColliderの実寸・すき間を読み取り専用で表示する
/// </summary>
class SelectionMeasurementsPanel {
  public:
	/// <summary>
	/// 現在の選択を測定し、値または測定不能の理由を表示する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">編集または再生シーン</param>
	/// <param name="ids">現在の選択順のID</param>
	/// <param name="open">表示状態。閉じるボタンでfalseにする</param>
	void Draw(const ImGuiSystem &ui, const Scene &scene,
	          std::span<const SceneObjectId> ids, bool &open) const;
};
