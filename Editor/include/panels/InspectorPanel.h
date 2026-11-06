#pragma once

#include "audio/AudioInspector.h"
#include "gameplay/GameplayInspector.h"
#include "math/Transform.h"
#include "scene/SceneObjectId.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

class ImGuiSystem;
class Scene;
class EditorHistory;
class ModelManager;
class AssetDatabase;

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
	/// Inspector描画後に表示対象または参照先の物体へ適用する操作
	/// </summary>
	enum class ObjectAction {
		NONE,
		COPY_TRANSFORM,
		COPY_RESPAWN_SETTINGS,
		PASTE_RESPAWN_SETTINGS,
		COPY_FOLLOW_SETTINGS,
		PASTE_FOLLOW_SETTINGS,
		FRAME_FOLLOW_PATH,
		COPY_CHARACTER_SETTINGS,
		PASTE_CHARACTER_SETTINGS,
		COPY_COLLIDER,
		PASTE_COLLIDER,
		PASTE_TRANSFORM,
		RESET_TRANSFORM,
		ALIGN_CAMERA_VIEW,
		MATCH_CAMERA_VIEW,
		ADD_CAMERA,
		MAKE_PRIMARY_CAMERA,
		COPY_CAMERA_LENS,
		PASTE_CAMERA_LENS,
		SELECT_OBJECT,
		PASTE_RESPAWN_POSITION,
		FRAME_RESPAWN,
		MOVE_TO_RESPAWN,
		FRAME_COLLIDER,
		SET_WORK_PLANE_FROM_COLLIDER,
		FRAME_JUMP,
		SELECT_GROUND_COLLIDER,
		SELECT_RESPAWN_BLOCKERS,
		SELECT_COLLIDER_OVERLAPS,
		ADD_MESH,
		REMOVE_MESH,
		ADD_MESH_RENDERER,
		REMOVE_MESH_RENDERER,
		APPLY_UV_TO_SELECTION,
		COPY_UV,
		CLEAR_UNUSED_TEXTURES,
		PASTE_UV_TO_SELECTION,
		PLAY_FROM_CAMERA,
		PREVIEW_CAMERA,
	};

	/// <summary>
	/// Inspectorの表示対象、または選択を要求した参照先の物体への操作
	/// </summary>
	struct ObjectRequest {
		ObjectAction action = ObjectAction::NONE;
		SceneObjectId objectId = 0;
	};

	/// <summary>
	/// このフレームの物体操作要求を取得する
	/// </summary>
	/// <returns>操作要求。操作がない場合はNONE</returns>
	const ObjectRequest &GetObjectRequest() const;

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
	/// 表示の固定を解除し、選択済み物体のInspectorを次の描画時に開く。選択自体は変更しない
	/// </summary>
	/// <param name="objectId">表示する選択物体ID。0は表示予約を取り消す</param>
	void RequestInspect(SceneObjectId objectId);

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
	/// <param name="models">材質スロットを表示する場合のモデル管理</param>
	/// <param name="assets">材質スロットを表示する場合のアセット一覧</param>
	/// <param name="canPasteCollider">貼り付けるBox Colliderのコピーを保持しているか</param>
	/// <param name="canPasteCameraLens">貼り付けるCameraのレンズ設定を保持しているか</param>
	/// <param name="canPasteCharacter">貼り付けるCharacter設定を保持しているか</param>
	/// <param name="canPasteFollow">貼り付ける追従設定を保持しているか</param>
	/// <param name="canPasteRespawn">貼り付ける復帰設定を保持しているか</param>
	/// <param name="canPasteUv">コピー済みの有効なUVがあるか</param>
	/// <param name="uvTargets">表示物体のUVを一括適用する選択先。空またはMesh Rendererなしの物体を含む場合は要求不可</param>
	/// <returns>true:物体を編集した false:シーンへの変更なし</returns>
	bool Draw(const ImGuiSystem &ui, Scene &scene,
	          SceneObjectId selectedObjectId, EditorHistory &history,
	          bool canPasteTransform = false, ModelManager *models = nullptr,
	          const AssetDatabase *assets = nullptr,
	          bool canPasteCollider = false, bool canPasteCameraLens = false,
	          bool canPasteCharacter = false, bool canPasteFollow = false,
	          bool canPasteRespawn = false,
	          std::span<const SceneObjectId> uvTargets = {},
	          bool canPasteUv = false);

	/// <summary>
	/// 選択した再生物体の現在値を表示し、Transformのコピーを返す。編集用の固定先・要求・履歴は変更しない
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">変更しない再生シーン</param>
	/// <param name="selectedObjectId">観察する物体ID。0または不在の場合は未選択を表示する</param>
	/// <returns>コピー操作をした時点のTransform。操作なし、対象なし、または無効な値ではnullopt</returns>
	std::optional<Transform> DrawReadOnly(const ImGuiSystem &ui,
	                                      const Scene &scene,
	                                      SceneObjectId selectedObjectId) const;

  private:
	GameplayInspector gameplayInspector_;
	AudioInspector audioInspector_;
	SceneObjectId lockedObjectId_ = 0;
	// 表示用の角度を保持し、編集中のオイラー角の切り替わりを防ぐ
	SceneObjectId rotationObjectId_ = 0;
	DirectX::XMFLOAT3 rotationDegrees_{};
	DirectX::XMFLOAT4 displayedRotation_{0.0f, 0.0f, 0.0f, 1.0f};
	SceneObjectId renameObjectId_ = 0;
	SceneObjectId inspectObjectId_ = 0;
	ModelRequest modelRequest_;
	VisibilityRequest visibilityRequest_;
	std::filesystem::path assetRevealPath_;
	ObjectRequest objectRequest_;
	SceneObjectId colliderFitRequest_ = 0;
	// 非表示のモデルも必要時に読み込み、同じ失敗はキャッシュ更新まで繰り返さない。
	std::filesystem::path slotModelPath_;
	std::uint64_t slotModelRevision_ = 0;
};
