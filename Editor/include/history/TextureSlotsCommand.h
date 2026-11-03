#pragma once

#include "graphics/material/TextureBindings.h"
#include "scene/SceneObjectId.h"
class Scene;

/// <summary>
/// メッシュを変更せず、物体の材質スロットの割り当てを記録する
/// </summary>
class TextureSlotsCommand {
  public:
	/// <summary>
	/// 画像の割り当ての変更前後を保持する
	/// </summary>
	/// <param name="id">対象の物体</param>
	/// <param name="before">変更前の全スロット</param>
	/// <param name="after">変更後の全スロット</param>
	TextureSlotsCommand(SceneObjectId id, TextureBindings before,
	                    TextureBindings after);

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
	bool Apply(Scene &scene, const TextureBindings &expected,
	           const TextureBindings &value) const;
	SceneObjectId id_;
	TextureBindings before_, after_;
};
