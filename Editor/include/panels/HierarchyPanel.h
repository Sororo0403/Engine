#pragma once

#include "scene/SceneObjectId.h"

#include <string>
#include <string_view>

class ImGuiSystem;
class Scene;

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
		DELETE_OBJECT,
	};

	/// <summary>
	/// Hierarchyの操作要求と入力状態
	/// </summary>
	struct Result {
		Action action = Action::NONE;
		SceneObjectId objectId = 0;

		bool windowFocused = false;
	};

	/// <summary>
	/// 物体の検索・一覧・操作用UIを構築する。
	/// 物体の追加・削除は実行せず、要求として返す
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">表示対象のシーン</param>
	/// <param name="selectedObjectId">選択中の物体ID。未選択は0</param>
	/// <returns>要求された操作と入力状態</returns>
	Result Draw(const ImGuiSystem &ui, const Scene &scene,
	            SceneObjectId &selectedObjectId);

	/// <summary>
	/// 検索文字列をクリアする。物体の選択状態は変更しない
	/// </summary>
	void Clear();

  private:
	/// <summary>
	/// 表示名が現在の検索条件に一致するか確認する
	/// </summary>
	/// <param name="name">物体の表示名</param>
	/// <returns>true:一致、または検索条件なし false:不一致</returns>
	bool MatchesFilter(std::string_view name) const;

	std::string filter_;
};
