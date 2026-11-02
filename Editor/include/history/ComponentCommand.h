#pragma once

#include "scene/Scene.h"
#include <optional>
#include <type_traits>

/// <summary>
/// 独立した任意コンポーネントの追加・変更・削除を記録する
/// </summary>
/// <typeparam name="T">IsValidと等値比較を持つコンポーネントの設定型</typeparam>
/// <typeparam name="Member">SceneObject内の対応するoptionalメンバー</typeparam>
template <class T, std::optional<T> SceneObject::*Member>
class ComponentCommand {
  public:
	/// <summary>
	/// 対象と編集前後の設定を保持する
	/// </summary>
	/// <param name="id">対象ID</param>
	/// <param name="before">変更前の設定</param>
	/// <param name="after">変更後の設定</param>
	ComponentCommand(SceneObjectId id, std::optional<T> before,
	                 std::optional<T> after)
	    : id_(id), before_(before), after_(after) {
	}

	/// <summary>
	/// 変更の有無を確認する
	/// </summary>
	/// <returns>true:変更あり false:同値</returns>
	bool HasChanges() const {
		return before_ != after_;
	}

	/// <summary>
	/// 編集前の設定へ戻す
	/// </summary>
	/// <param name="scene">対象シーン</param>
	/// <returns>true:成功 false:対象・想定値・設定が無効</returns>
	bool Undo(Scene &scene) const {
		return Apply(scene, after_, before_);
	}

	/// <summary>
	/// 編集後の設定を再適用する
	/// </summary>
	/// <param name="scene">対象シーン</param>
	/// <returns>true:成功 false:対象・想定値・設定が無効</returns>
	bool Redo(Scene &scene) const {
		return Apply(scene, before_, after_);
	}

  private:
	/// <summary>
	/// 現在値を確認してから値を置き換える
	/// </summary>
	/// <param name="scene">対象シーン</param>
	/// <param name="expected">想定する現在値</param>
	/// <param name="value">変更する値</param>
	/// <returns>true:成功 false:不一致または無効</returns>
	bool Apply(Scene &scene, const std::optional<T> &expected,
	           const std::optional<T> &value) const {
		auto *object = scene.FindObject(id_);

		if (!object || object->*Member != expected ||
		    (value && !value->IsValid())) {
			return false;
		}

		// 材質の配列などを先にコピーし、確保失敗時に現在のコンポーネントを保持する。
		auto next = value;
		static_assert(std::is_nothrow_swappable_v<std::optional<T>>);
		(object->*Member).swap(next);
		return true;
	}

	SceneObjectId id_;
	std::optional<T> before_, after_;
};

using CameraCommand = ComponentCommand<CameraComponent, &SceneObject::camera>;

using CharacterCommand =
    ComponentCommand<CharacterSettings, &SceneObject::character>;
using FollowCameraCommand =
    ComponentCommand<FollowCameraSettings, &SceneObject::followCamera>;
using RespawnCommand = ComponentCommand<RespawnSettings, &SceneObject::respawn>;

using ColorTintCommand = ComponentCommand<ColorTint, &SceneObject::colorTint>;

using MeshRendererCommand =
    ComponentCommand<MeshRendererComponent, &SceneObject::meshRenderer>;
