#pragma once

#include "panels/HierarchyFilter.h"
#include "scene/SceneObjectId.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

class ImGuiSystem;
class Scene;
class EditorSelection;

/// <summary>
/// シーン上の物体の一覧・検索・選択・操作要求を管理する
/// </summary>
class HierarchyPanel {
  public:
	/// <summary>
	/// 検索・表示条件を解除し、指定物体を表示する。選択状態は変更しない
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
		DELETE_OBJECTS,
		COPY_OBJECTS,
		CUT_OBJECTS,
		DUPLICATE_OBJECTS,
		RESET_TRANSFORMS,
		DROP_TO_GROUND,
		ALIGN_X,
		ALIGN_Y,
		ALIGN_Z,
		DISTRIBUTE_X,
		DISTRIBUTE_Y,
		DISTRIBUTE_Z,
		RENAME_OBJECT,
		FRAME_SELECTED,
		HIDE_OBJECTS,
		SHOW_OBJECTS,
	};

	/// <summary>
	/// Hierarchyの操作要求と入力状態
	/// </summary>
	struct Result {
		Action action = Action::NONE;

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
	/// <returns>要求された操作と入力状態</returns>
	Result Draw(const ImGuiSystem &ui, const Scene &scene,
	            EditorSelection &selection);

	/// <summary>
	/// 検索・並び順・表示条件・表示要求・範囲選択の基準をクリアする。選択状態は変更しない
	/// </summary>
	void Clear();

	/// <summary>
	/// 現在の絞り込み条件に一致する物体を全て選択する
	/// </summary>
	/// <param name="scene">表示対象のシーン</param>
	/// <param name="selection">変更する選択状態</param>
	/// <returns>true:選択変更あり false:変更なし</returns>
	bool SelectAllVisible(const Scene &scene, EditorSelection &selection);

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

	HierarchyFilter filter_;
	SortMode sortMode_ = SortMode::CREATION;
	SceneObjectId rangeAnchorId_ = 0;
	SceneObjectId lastActiveId_ = 0;
	SceneObjectId scrollToId_ = 0;
	bool revealRequested_ = false;
};
