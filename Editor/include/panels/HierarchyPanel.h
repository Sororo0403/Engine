#pragma once

#include "scene/SceneObjectId.h"

#include <span>
#include <string>
#include <string_view>

class ImGuiSystem;
class Scene;
class EditorSelection;

/// <summary>
/// シーン上の物体の一覧・検索・選択・操作要求を管理する
/// </summary>
class HierarchyPanel {
  public:
	/// <summary>
	/// Hierarchyから要求する操作
	/// </summary>
	enum class Action {
		NONE,
		CREATE_EMPTY,
		DELETE_OBJECTS,
		COPY_OBJECTS,
		CUT_OBJECTS,
		DUPLICATE_OBJECTS,
		RESET_TRANSFORMS,
		RENAME_OBJECT,
		FRAME_SELECTED,
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
	/// 検索文字列と範囲選択の基準をクリアする。物体の選択状態は変更しない
	/// </summary>
	void Clear();

	/// <summary>
	/// 現在の検索条件に一致する物体を全て選択する
	/// </summary>
	/// <param name="scene">表示対象のシーン</param>
	/// <param name="selection">変更する選択状態</param>
	/// <returns>true:選択変更あり false:変更なし</returns>
	bool SelectAllVisible(const Scene &scene, EditorSelection &selection);

  private:
	/// <summary>
	/// 表示名が現在の検索条件に一致するか確認する
	/// </summary>
	/// <param name="name">物体の表示名</param>
	/// <returns>true:一致、または検索条件なし false:不一致</returns>
	bool MatchesFilter(std::string_view name) const;

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
	                  std::span<const SceneObjectId> visibleIds,
	                  bool control, bool shift, EditorSelection &selection);

	std::string filter_;
	SceneObjectId rangeAnchorId_ = 0;
	SceneObjectId lastActiveId_ = 0;
};
