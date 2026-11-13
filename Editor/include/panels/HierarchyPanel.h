#pragma once

#include "panels/HierarchyFilter.h"
#include "scene/SceneObjectId.h"

#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class ImGuiSystem;
class Scene;
struct SceneObject;
class EditorSelection;

/// <summary>
/// シーン上の物体の一覧・検索・選択・操作要求を管理する
/// </summary>
class HierarchyPanel {
  public:
	/// <summary>
	/// 指定物体を表示する。条件外なら絞り込みを一時解除する。選択状態は変更しない
	/// </summary>
	/// <param name="objectId">表示する物体のID。0の場合は表示要求を取り消す</param>
	void RequestReveal(SceneObjectId objectId);

	/// <summary>
	/// Hierarchyから要求する操作
	/// </summary>
	enum class Action {
		NONE,
		CREATE_EMPTY,
		CREATE_BOX,
		CREATE_CAMERA,
		DELETE_OBJECTS,
		COPY_OBJECTS,
		CUT_OBJECTS,
		DUPLICATE_OBJECTS,
		DUPLICATE_ARRAY,
		PASTE_COLLIDERS,
		PASTE_CAMERA_LENSES,
		PASTE_RESPAWN_SETTINGS,
		RESET_TRANSFORMS,
		SNAP_POSITIONS,
		DROP_TO_GROUND,
		DROP_TO_SURFACE,
		PLACE_ON_WORK_PLANE,
		ALIGN_X,
		ALIGN_Y,
		ALIGN_Z,
		ALIGN_COLLIDER_MIN_X,
		ALIGN_COLLIDER_MAX_X,
		ALIGN_COLLIDER_MIN_Y,
		ALIGN_COLLIDER_MAX_Y,
		ALIGN_COLLIDER_MIN_Z,
		ALIGN_COLLIDER_MAX_Z,
		DISTRIBUTE_X,
		DISTRIBUTE_Y,
		DISTRIBUTE_Z,
		DISTRIBUTE_COLLIDER_X,
		DISTRIBUTE_COLLIDER_Y,
		DISTRIBUTE_COLLIDER_Z,
		RENAME_OBJECT,
		FRAME_SELECTED,
		SELECT_FOLLOW_CAMERAS,
		HIDE_OBJECTS,
		SHOW_OBJECTS,
		ENABLE_OBJECTS,
		DISABLE_OBJECTS,
		REPARENT_OBJECTS,
	};

	/// <summary>
	/// Hierarchyの操作要求と入力状態
	/// </summary>
	struct Result {
		Action action = Action::NONE;
		// 親変更は入力時点の対象を保持し、選択変更と独立した要求として返す。
		std::vector<SceneObjectId> parentObjects;
		SceneObjectId parentId = 0;
		bool keepWorld = true;

		bool windowFocused = false;
		bool selectionChanged = false;
	};

	/// <summary>
	/// 物体の検索・一覧・操作用UIを構築する。
	/// 物体の追加・削除は実行せず、要求として返す
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">表示対象のシーン</param>
	/// <param name="selection">Editorの選択状態</param>
	/// <param name="editable">追加・削除などの編集要求を許可するか。falseでも選択・検索・フレーミングは許可する</param>
	/// <param name="canPasteCollider">貼り付けるColliderのコピーを保持しているか</param>
	/// <param name="canPasteCameraLens">貼り付けるCameraのレンズ設定を保持しているか</param>
	/// <param name="canPasteRespawn">貼り付ける復帰設定を保持しているか</param>
	/// <returns>要求された操作と入力状態</returns>
	Result Draw(const ImGuiSystem &ui, const Scene &scene,
	            EditorSelection &selection, bool editable = true,
	            bool canPasteCollider = false, bool canPasteCameraLens = false,
	            bool canPasteRespawn = false);

	/// <summary>
	/// 検索・並び順・表示条件・表示要求・範囲選択の基準とScene選択の除外をクリアする。選択状態は変更しない
	/// </summary>
	void Clear();

	/// <summary>
	/// Sceneビューのマウス選択から除く物体IDを取得する。描画やHierarchyの選択は制限しない
	/// </summary>
	/// <returns>次の選択可否変更・Clearまで有効なID一覧。削除後もUndoに備えて保持する</returns>
	std::span<const SceneObjectId> GetScenePickingDisabledIds() const;

	/// <summary>
	/// 現在の絞り込み条件に一致する物体を全て選択する
	/// </summary>
	/// <param name="scene">表示対象のシーン</param>
	/// <param name="selection">変更する選択状態</param>
	/// <returns>true:選択変更あり false:変更なし</returns>
	bool SelectAllVisible(const Scene &scene, EditorSelection &selection);

	/// <summary>
	/// 現在の一覧の選択を反転する。条件外の選択順を保ち、新しく選ぶ項目を表示順で末尾へ加える
	/// </summary>
	/// <param name="scene">表示対象のシーン。シーンや履歴は変更しない</param>
	/// <param name="selection">変更する選択状態。末尾をアクティブとし、次の範囲選択の基準にする</param>
	/// <returns>true:選択変更あり false:一覧が空、または選択を設定できない</returns>
	bool InvertVisible(const Scene &scene, EditorSelection &selection);

  private:
	/// <summary>
	/// 一覧の表示順。同名の物体はIDの昇順を維持する
	/// </summary>
	enum class SortMode { CREATION, NAME_ASCENDING, NAME_DESCENDING };

	/// <summary>
	/// 絞り込み条件と並び順に沿った表示対象を作る
	/// </summary>
	/// <param name="scene">表示するシーン</param>
	/// <returns>現在の表示順の物体ID一覧</returns>
	std::vector<SceneObjectId> GetVisibleIds(const Scene &scene) const;

	/// <summary>
	/// Hierarchyに届いた移動キーを、表示順に沿って選択へ反映する
	/// </summary>
	/// <param name="visibleIds">現在の表示順の物体ID一覧</param>
	/// <param name="selection">変更する選択状態</param>
	/// <param name="focusId">キーボードで移動した項目。操作がない場合は0</param>
	/// <returns>true:選択変更あり false:変更なし、または入力対象外</returns>
	bool NavigateSelection(std::span<const SceneObjectId> visibleIds,
	                       EditorSelection &selection, SceneObjectId &focusId);

	/// <summary>
	/// 表示項目へのクリックを、修飾キーに応じて選択へ反映する
	/// </summary>
	/// <param name="objectId">クリックした物体ID</param>
	/// <param name="visibleIds">現在の表示順の物体ID一覧</param>
	/// <param name="control">Ctrlキーが押されているか</param>
	/// <param name="shift">Shiftキーが押されているか</param>
	/// <param name="selection">変更する選択状態</param>
	/// <returns>true:選択変更あり false:変更なし、または対象が表示されていない</returns>
	bool SelectObject(SceneObjectId objectId,
	                  std::span<const SceneObjectId> visibleIds, bool control,
	                  bool shift, EditorSelection &selection);

	/// <summary>
	/// 指定した物体群をSceneビューの選択対象から除外・復帰する。シーンや履歴は変更しない
	/// </summary>
	/// <param name="scene">物体の存在を確認するシーン</param>
	/// <param name="ids">対象のID。重複はまとめる</param>
	/// <param name="disabled">true:選択から除く false:選択を許可する</param>
	/// <returns>true:設定成功 false:無効・不在のIDを含む。失敗時は以前の除外を保持する</returns>
	bool SetScenePickingDisabled(const Scene &scene,
	                             std::span<const SceneObjectId> ids,
	                             bool disabled);

	/// <summary>
	/// 検索・コンポーネント・表示状態と、Scene選択除外の絞り込みをまとめて判定する
	/// </summary>
	/// <param name="id">判定する物体ID</param>
	/// <param name="object">一覧に表示する物体</param>
	/// <returns>true:全ての条件に一致 false:条件外</returns>
	bool MatchesFilters(SceneObjectId id, const SceneObject &object) const;

	/// <summary>
	/// 一覧が絞り込み中か確認する。絞り込み中は一致した項目だけを平坦に表示する
	/// </summary>
	/// <returns>true:絞り込み中 false:親子ツリー表示</returns>
	bool IsFiltered() const;

	struct FilterState {
		HierarchyFilter filter;
		bool pickingDisabledOnly = false;
	};

	HierarchyFilter filter_;
	std::optional<FilterState> previousFilters_;
	std::set<SceneObjectId> collapsed_;
	std::vector<SceneObjectId> scenePickingDisabled_;
	SortMode sortMode_ = SortMode::CREATION;
	SceneObjectId rangeAnchorId_ = 0;
	SceneObjectId lastActiveId_ = 0;
	SceneObjectId scrollToId_ = 0;
	bool revealRequested_ = false;
	bool pickingDisabledOnly_ = false;
};
