#pragma once

#include "math/Transform.h"
#include "scene/SceneObjectId.h"

#include <filesystem>

class ImGuiSystem;
class Scene;
class EditorHistory;

/// <summary>
/// 選択に追従、または固定した物体の情報表示と編集要求を管理する
/// </summary>
class InspectorPanel {
  public:
	/// <summary>
	/// 表示の固定・名前欄のフォーカス・編集要求・回転の表示状態をクリアする
	/// </summary>
	void Clear();

	/// <summary>
	/// Transform見出しから要求する操作
	/// </summary>
	enum class TransformAction {
		NONE,
		COPY,
		PASTE,
		RESET,
	};

	/// <summary>
	/// Inspectorに表示中の物体だけを対象としたTransform操作の要求
	/// </summary>
	struct TransformRequest {
		TransformAction action = TransformAction::NONE;
		SceneObjectId objectId = 0;
	};

	/// <summary>
	/// このフレームのTransform操作要求を取得する
	/// </summary>
	/// <returns>操作要求。操作がない場合はNONE</returns>
	const TransformRequest &GetTransformRequest() const;

	/// <summary>
	/// 表示対象のモデルの差し替え、または参照解除の要求
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
	/// このフレームでAssetsへの表示を要求したモデルを取得する
	/// </summary>
	/// <returns>対象モデルのパス。要求なしの場合は空</returns>
	const std::filesystem::path &GetAssetRevealPath() const;

	/// <summary>
	/// 表示対象のモデルの表示状態の変更要求
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
	/// このフレームに衝突形状の追加・モデル合わせを要求した物体を取得する
	/// </summary>
	/// <returns>対象ID。要求なしの場合は0</returns>
	SceneObjectId GetColliderFitRequest() const;

	/// <summary>
	/// 表示の固定を解除し、指定した選択物体の名前欄へのフォーカスを次の描画時に予約する
	/// </summary>
	/// <param name="objectId">名前を編集する選択物体のID</param>
	void RequestRename(SceneObjectId objectId);

	/// <summary>
	/// 選択に追従、または固定した物体の編集用UIを構築する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">編集対象のシーン</param>
	/// <param name="selectedObjectId">追従先の選択物体ID。未選択は0。固定中は固定先を編集する</param>
	/// <param name="history">編集内容を記録する履歴</param>
	/// <param name="canPasteTransform">貼り付けるTransformを保持しているか</param>
	/// <returns>true:物体を編集した false:シーンへの変更なし</returns>
	bool Draw(const ImGuiSystem &ui, Scene &scene,
	          SceneObjectId selectedObjectId, EditorHistory &history,
	          bool canPasteTransform = false);

  private:
	SceneObjectId lockedObjectId_ = 0;
	// 表示用の角度を保持し、編集中のオイラー角の切り替わりを防ぐ
	SceneObjectId rotationObjectId_ = 0;
	DirectX::XMFLOAT3 rotationDegrees_{};
	DirectX::XMFLOAT4 displayedRotation_{0.0f, 0.0f, 0.0f, 1.0f};
	SceneObjectId renameObjectId_ = 0;
	ModelRequest modelRequest_;
	VisibilityRequest visibilityRequest_;
	std::filesystem::path assetRevealPath_;
	TransformRequest transformRequest_;
	SceneObjectId colliderFitRequest_ = 0;
};
