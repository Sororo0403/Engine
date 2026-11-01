#pragma once

#include "history/ObjectRename.h"

#include <span>
#include <string>
#include <vector>

class ImGuiSystem;
class Scene;

/// <summary>
/// 一括リネームダイアログの選択結果
/// </summary>
enum class BatchRenameResult {
	NONE,
	ACCEPTED,
	CANCELLED,
};

/// <summary>
/// 選択物体を名前と連番でリネームするプレビューを管理する
/// </summary>
class BatchRenameDialog {
  public:
	/// <summary>
	/// 対象と元の名前を保持し、Hierarchyと同じID順に表示を予約する
	/// </summary>
	/// <param name="scene">名前を取得するシーン</param>
	/// <param name="objectIds">重複のない対象ID一覧</param>
	/// <returns>true:表示を予約 false:対象なし、重複、または削除済み</returns>
	bool Show(const Scene &scene, std::span<const SceneObjectId> objectIds);

	/// <summary>
	/// 変更後の名前と適用・取り消しの操作を描画する。シーンは変更しない
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <returns>選択結果。未確定の場合はNONE</returns>
	BatchRenameResult Draw(const ImGuiSystem &ui);

	/// <summary>
	/// 確定した名前の変更要求を取得する
	/// </summary>
	/// <returns>ACCEPTED後、次のShowまたはDrawまで有効な変更一覧</returns>
	std::span<const ObjectRename> GetRenames() const;

  private:
	/// <summary>
	/// 入力を検証し、全対象の変更後の名前を組み立てる
	/// </summary>
	void RebuildPreview();

	std::vector<ObjectRename> renames_;
	std::string baseName_ = "Object";
	int firstNumber_ = 1;
	std::string error_;
	bool openRequested_ = false;
	bool focusRequested_ = false;
};
