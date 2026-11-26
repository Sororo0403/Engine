#include "history/ParentCommand.h"

#include "math/TransformUtils.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"

namespace {
/// <summary>
/// 現在値を確認してから、親とローカル変形を同時に反映する
/// </summary>
/// <param name="scene">編集対象</param>
/// <param name="id">物体ID</param>
/// <param name="expectedParent">履歴が期待する現在の親</param>
/// <param name="parent">変更先の親</param>
/// <param name="expected">履歴が期待する現在のローカル変形</param>
/// <param name="local">反映するローカル変形</param>
/// <returns>true:成功 false:不一致、または無効な関係・変形。失敗時は現在値を保持する</returns>
bool Apply(Scene &scene, SceneObjectId id, SceneObjectId expectedParent,
           SceneObjectId parent, const Transform &expected,
           const Transform &local) {
	const auto *object = scene.FindObject(id);
	if (!object || object->GetParentId() != expectedParent ||
	    !TransformUtils::AreEqual(object->transform, expected) ||
	    !TransformUtils::IsValid(local))
		return false;
	return scene.ApplyTransaction([&](Scene &working) {
		auto *candidate = working.FindObject(id);
		candidate->transform = local;
		if (!SceneHierarchy::SetParent(working, id, parent, false))
			return false;
		SceneHierarchy::WorldTransforms transforms;
		return SceneHierarchy::BuildWorldTransforms(working, transforms);
	});
}
} // namespace

ParentCommand::ParentCommand(SceneObjectId id, SceneObjectId beforeParent,
                             SceneObjectId afterParent, const Transform &before,
                             const Transform &after) noexcept
    : objectId_(id), beforeParent_(beforeParent), afterParent_(afterParent),
      before_(before), after_(after) {
}

bool ParentCommand::Undo(Scene &scene) const {
	return Apply(scene, objectId_, afterParent_, beforeParent_, after_,
	             before_);
}

bool ParentCommand::Redo(Scene &scene) const {
	return Apply(scene, objectId_, beforeParent_, afterParent_, before_,
	             after_);
}
