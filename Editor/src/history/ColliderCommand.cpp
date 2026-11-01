#include "history/ColliderCommand.h"

#include "physics/BoxColliderUtils.h"
#include "scene/Scene.h"

namespace {
/// <summary>
/// 対象の現在値と変更後の形状を確認して、衝突設定を変更する
/// </summary>
/// <param name="scene">編集するシーン</param>
/// <param name="id">対象ID</param>
/// <param name="expected">現在値の想定</param>
/// <param name="value">変更後の設定</param>
/// <returns>true:成功 false:対象や設定が不一致、または形状が無効</returns>
bool Apply(Scene &scene, SceneObjectId id,
           const std::optional<BoxCollider> &expected,
           const std::optional<BoxCollider> &value) {
	auto *object = scene.FindObject(id);
	if (!object || object->collider != expected) {
		return false;
	}
	if (value && !BoxColliderUtils::IsValid(*value)) {
		return false;
	}
	object->collider = value;
	return true;
}
} // namespace

ColliderCommand::ColliderCommand(SceneObjectId objectId,
                                 std::optional<BoxCollider> before,
                                 std::optional<BoxCollider> after)
    : objectId_(objectId), before_(before), after_(after) {}

bool ColliderCommand::Undo(Scene &scene) const {
	return Apply(scene, objectId_, after_, before_);
}
bool ColliderCommand::Redo(Scene &scene) const {
	return Apply(scene, objectId_, before_, after_);
}
