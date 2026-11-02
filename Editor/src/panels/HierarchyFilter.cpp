#include "panels/HierarchyFilter.h"

#include "scene/SceneObject.h"
#include <string_view>

bool HierarchyFilter::Matches(const SceneObject &object) const {
	const std::string_view name =
	    object.name.empty() ? std::string_view{"Object"} : object.name;

	if ((hiddenOnly && object.visible) ||
	    name.find(text) == std::string_view::npos) {
		return false;
	}

	switch (component) {
	case Component::ALL:
		return true;
	case Component::BOX_COLLIDER:
		return object.collider.has_value();
	case Component::MODELS_WITHOUT_COLLIDER:
		return !object.modelPath.empty() && !object.collider;
	case Component::CHARACTER:
		return object.character.has_value();
	case Component::CAMERA:
		return object.camera.has_value();
	case Component::FOLLOW_CAMERA:
		return object.followCamera.has_value();
	case Component::RESPAWN:
		return object.respawn.has_value();
	default:
		return false;
	}
}

void HierarchyFilter::Clear() noexcept {
	text.clear();
	hiddenOnly = false;
	component = Component::ALL;
}
