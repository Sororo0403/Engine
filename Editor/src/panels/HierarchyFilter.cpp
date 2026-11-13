#include "panels/HierarchyFilter.h"

#include "scene/SceneObject.h"
#include <charconv>
#include <string_view>

bool HierarchyFilter::Matches(SceneObjectId id,
                              const SceneObject &object) const {
	if (id == 0) {
		return false;
	}

	const std::string_view name =
	    object.name.empty() ? std::string_view{"Object"} : object.name;

	bool matchesText = name.find(text) != std::string_view::npos;
	const auto trim = [](std::string_view value) {
		constexpr std::string_view spaces = " \t\r\n";
		const auto first = value.find_first_not_of(spaces);
		return first == std::string_view::npos
		           ? std::string_view{}
		           : value.substr(first,
		                          value.find_last_not_of(spaces) - first + 1);
	};
	const auto query = trim(text);

	if (text.starts_with("tag:")) {
		matchesText = !object.tag.empty() &&
		              object.tag == std::string_view{text}.substr(4);
	} else if (query.size() >= 3 && (query[0] == 'i' || query[0] == 'I') &&
	           (query[1] == 'd' || query[1] == 'D') && query[2] == ':') {
		const auto number = trim(query.substr(3));
		SceneObjectId requested = 0;
		matchesText = false;

		if (!number.empty()) {
			const auto end = number.data() + number.size();
			const auto parsed = std::from_chars(number.data(), end, requested);
			matchesText = parsed.ec == std::errc{} && parsed.ptr == end &&
			              requested != 0 && requested == id;
		}
	}

	if ((hiddenOnly && !object.IsHidden()) || !matchesText) {
		return false;
	}

	switch (component) {
	case Component::ALL:
		return true;
	case Component::BOX_COLLIDER:
		return object.collider.has_value();
	case Component::MODELS_WITHOUT_COLLIDER:
		return object.HasMesh() && !object.collider;
	case Component::CHARACTER:
		return object.character.has_value();
	case Component::CAMERA:
		return object.camera.has_value();
	case Component::FOLLOW_CAMERA:
		return object.followCamera.has_value();
	case Component::RESPAWN:
		return object.respawn.has_value();
	case Component::MESH:
		return object.mesh.has_value();
	case Component::MESH_RENDERER:
		return object.meshRenderer.has_value();
	case Component::PRIMARY_CAMERA:
		return object.camera && object.camera->primary;
	case Component::INCOMPLETE_RENDERING:
		return (object.mesh || object.meshRenderer) &&
		       (!object.HasMesh() || !object.meshRenderer);
	case Component::CHARACTERS_WITHOUT_COLLIDER:
		return object.character && !object.collider;
	case Component::COLOR_TINT:
		return object.colorTint.has_value();
	case Component::ANIMATION_PLAYER:
		return object.animationClip.has_value();
	case Component::ANIMATOR:
		return object.animator.has_value();
	case Component::AUDIO_LISTENER:
		return object.audioListener.has_value();
	case Component::AUDIO_SOURCE:
		return object.audioSource.has_value();
	case Component::CANVAS:
		return object.canvas.has_value();
	case Component::DIRECTIONAL_LIGHT:
		return object.directionalLight.has_value();
	case Component::LUA_SCRIPT:
		return object.script.has_value();
	case Component::MOTION_PATH:
		return object.motionPath.has_value();
	case Component::PARTICLE_EMITTER:
		return object.particleEmitter.has_value();
	case Component::POINT_LIGHT:
		return object.pointLight.has_value();
	case Component::PREFAB_INSTANCE:
		return object.prefabLink.has_value();
	case Component::RECT_TRANSFORM:
		return object.rectTransform.has_value();
	case Component::RIGID_BODY:
		return object.rigidBody.has_value();
	case Component::SPOT_LIGHT:
		return object.spotLight.has_value();
	case Component::TRANSFORM_ANIMATION:
		return object.transformAnimation.has_value();
	case Component::TRIGGER_ACTIONS:
		return object.triggerActions.has_value();
	case Component::UI_BUTTON:
		return object.button.has_value();
	case Component::UI_IMAGE:
		return object.image.has_value();
	case Component::UI_TEXT:
		return object.text.has_value();
	}

	return false;
}

void HierarchyFilter::Clear() noexcept {
	text.clear();
	hiddenOnly = false;
	component = Component::ALL;
}
