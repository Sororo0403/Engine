#include "scene/SceneObjectReferences.h"
#include "scene/SceneObject.h"
#include <algorithm>

namespace {
/// <summary>
/// 対象IDを持つ各コンポーネントを、複製・削除・コピーで共通に列挙する
/// </summary>
/// <typeparam name="T">constまたは変更可能なSceneObject</typeparam>
/// <typeparam name="F">対象IDを受け取る処理</typeparam>
/// <param name="object">列挙する物体</param>
/// <param name="visit">各対象IDに適用する処理</param>
template <class T, class F> void Visit(T &object, F visit) {
	if (object.prefabLink) {
		visit(object.prefabLink->root);
	}
	if (object.followCamera) {
		visit(object.followCamera->target);
	}
	if (object.triggerActions) {
		for (auto &action : object.triggerActions->actions) {
			visit(action.target);
			visit(action.destination);
		}
	}
}
} // namespace

void SceneObjectReferences::Remap(
    SceneObject &object, const std::map<SceneObjectId, SceneObjectId> &remap) {
	if (object.prefabLink && remap.contains(object.prefabLink->root) &&
	    !object.prefabLink->objects.empty()) {
		for (auto &[source, current] : object.prefabLink->objects) {
			(void)source;
			const auto found = remap.find(current);
			current = found == remap.end() ? 0 : found->second;
		}
	}
	Visit(object, [&](SceneObjectId &target) {
		const auto found = remap.find(target);
		if (target != 0 && found != remap.end()) {
			target = found->second;
		}
	});
}

bool SceneObjectReferences::HasExternal(
    const SceneObject &object,
    std::span<const SceneObjectId> allowed) noexcept {
	bool result = false;
	Visit(object, [&](SceneObjectId target) {
		result |= target != 0 && std::find(allowed.begin(), allowed.end(),
		                                   target) == allowed.end();
	});
	return result;
}

void SceneObjectReferences::ClearExternal(
    SceneObject &object, std::span<const SceneObjectId> allowed) noexcept {
	if (object.prefabLink &&
	    std::find(allowed.begin(), allowed.end(), object.prefabLink->root) ==
	        allowed.end()) {
		object.prefabLink.reset();
	}
	Visit(object, [&](SceneObjectId &target) {
		if (target != 0 && std::find(allowed.begin(), allowed.end(), target) ==
		                       allowed.end()) {
			target = 0;
		}
	});
}

void SceneObjectReferences::ClearTargets(
    SceneObject &object, std::span<const SceneObjectId> removed) noexcept {
	if (object.prefabLink &&
	    std::find(removed.begin(), removed.end(), object.prefabLink->root) !=
	        removed.end()) {
		object.prefabLink.reset();
	}
	Visit(object, [&](SceneObjectId &target) {
		if (std::find(removed.begin(), removed.end(), target) !=
		    removed.end()) {
			target = 0;
		}
	});
}
