#pragma once

#include "math/Transform.h"
#include "scene/SceneObjectId.h"

#include <filesystem>

class ImGuiSystem;
class Scene;
class EditorHistory;

/// <summary>
/// 選択した物体の情報表示と編集要求を管理する
/// </summary>
class InspectorPanel {
  public:
	/// <summary>
	/// 選択物体のモデルの差し替え、または参照解除の要求
	/// </summary>
	struct ModelRequest {
		SceneObjectId objectId = 0;
		std::filesystem::path path;
	};

	/// <summary>
	/// このフレームのモデル参照の変更要求を取得する
	/// </summary>
	/// <returns>変更要求。操作がない場合のIDは0、参照解除の場合のパスは空</returns>
	const ModelRequest &GetModelRequest() const;

	/// <summary>
	/// 選択物体のモデルの表示状態の変更要求
	/// </summary>
	struct VisibilityRequest {
		SceneObjectId objectId = 0;
		bool visible = true;
	};

	/// <summary>
	/// このフレームの表示状態の変更要求を取得する
	/// </summary>
	/// <returns>変更要求。操作がない場合のIDは0</returns>
	const VisibilityRequest &GetVisibilityRequest() const;
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
	ModelRequest modelRequest_;
	VisibilityRequest visibilityRequest_;
};
