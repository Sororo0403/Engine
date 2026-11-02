#pragma once

#include "scene/SceneObjectId.h"
#include <optional>
#include <string>

class ImGuiSystem;
class Scene;

/// <summary>
/// Follow Cameraの追従先を名前・IDで検索し、選択要求を返す
/// </summary>
class FollowTargetPicker {
  public:
	/// <summary>
	/// 追従先の検索付き一覧を表示する。自己参照・循環・不在の連鎖は選択できない
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">候補を調べるシーン。値は変更しない</param>
	/// <param name="owner">Follow Cameraの物体ID</param>
	/// <param name="current">現在の追従先。0は未設定</param>
	/// <returns>選択されたID。0は解除、nulloptは選択なしまたはキャンセル</returns>
	std::optional<SceneObjectId> Draw(const ImGuiSystem &ui, const Scene &scene,
	                                  SceneObjectId owner,
	                                  SceneObjectId current);

	/// <summary>
	/// 保持している検索文字列を解除する
	/// </summary>
	void Clear() noexcept;

  private:
	std::string search_;
};
