#pragma once

#include "math/Transform.h"
#include "scene/SceneObjectId.h"

class ImGuiSystem;
class Scene;
class EditorHistory;

/// <summary>
/// 選択した物体の情報表示と変形情報の編集を管理する
/// </summary>
class InspectorPanel {
  public:
	/// <summary>
	/// 指定した物体の名前欄へのフォーカスを、次の描画時に予約する
	/// </summary>
	/// <param name="objectId">名前を編集する選択物体のID</param>
	void RequestRename(SceneObjectId objectId);

	/// <summary>
	/// 選択した物体の編集用UIを構築する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">編集対象のシーン</param>
	/// <param name="selectedObjectId">選択中の物体ID。未選択は0</param>
	/// <param name="history">編集内容を記録する履歴</param>
	/// <returns>true:物体を編集した false:シーンへの変更なし</returns>
	bool Draw(const ImGuiSystem &ui, Scene &scene,
	          SceneObjectId selectedObjectId, EditorHistory &history);

  private:
	// 表示用の角度を保持し、編集中のオイラー角の切り替わりを防ぐ
	SceneObjectId rotationObjectId_ = 0;
	DirectX::XMFLOAT3 rotationDegrees_{};
	DirectX::XMFLOAT4 displayedRotation_{0.0f, 0.0f, 0.0f, 1.0f};
	SceneObjectId renameObjectId_ = 0;
};
