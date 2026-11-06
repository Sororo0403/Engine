#pragma once
#include "scene/SceneObjectId.h"
#include <filesystem>
#include <span>
class EditorHistory;
class ImGuiSystem;
struct SceneObject;

/// <summary>
/// Canvas・Rect Transform・Text・Image・Buttonを編集し、連続した変更を1回の履歴へまとめる
/// </summary>
class UIInspector {
  public:
	/// <summary>
	/// 画面上のUIに使用するコンポーネントの設定を表示する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="id">編集する物体</param>
	/// <param name="object">編集対象</param>
	/// <param name="history">変更を記録する履歴</param>
	/// <param name="images">UIへ割り当て可能な画像ファイル</param>
	/// <returns>true:編集あり false:変更なし</returns>
	bool Draw(const ImGuiSystem &ui, SceneObjectId id, SceneObject &object,
	          EditorHistory &history,
	          std::span<const std::filesystem::path> images) const;
	/// <summary>
	/// 再生物体のUI設定と、Luaによる現在の表示内容を読み取り専用で表示する
	/// </summary>
	/// <param name="ui">表示するUI</param>
	/// <param name="object">再生中の物体</param>
	void DrawReadOnly(const ImGuiSystem &ui, const SceneObject &object) const;
};
