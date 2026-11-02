#pragma once

#include "graphics/material/MaterialBindings.h"
#include "scene/SceneObjectId.h"
class Scene;

/// <summary>
/// メッシュを変更せず、物体の材質スロットの割り当てを記録する
/// </summary>
class MaterialSlotsCommand {
  public:
	/// <summary>
	/// 材質の割り当ての変更前後を保持する
	/// </summary>
	/// <param name="id">対象の物体</param>
	/// <param name="before">変更前の全スロット</param>
	/// <param name="after">変更後の全スロット</param>
	MaterialSlotsCommand(SceneObjectId id, MaterialBindings before,
	                     MaterialBindings after);

	/// <summary>
	/// 変更前の割り当てへ戻す
	/// </summary>
	/// <param name="scene">対象シーン</param>
	/// <returns>true:成功 false:対象・現在値・設定が無効</returns>
	bool Undo(Scene &scene) const;

	/// <summary>
	/// 変更後の割り当てを適用する
	/// </summary>
	/// <param name="scene">対象シーン</param>
	/// <returns>true:成功 false:対象・現在値・設定が無効</returns>
	bool Redo(Scene &scene) const;

  private:
	bool Apply(Scene &scene, const MaterialBindings &expected,
	           const MaterialBindings &value) const;
	SceneObjectId id_;
	MaterialBindings before_, after_;
};
