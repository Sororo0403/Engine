#include "panels/HierarchyFilter.h"

#include "scene/SceneObject.h"
#include <charconv>
#include <string_view>

bool HierarchyFilter::Matches(SceneObjectId id,
                              const SceneObject &object) const {
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

	if (query.size() >= 3 && (query[0] == 'i' || query[0] == 'I') &&
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

	if ((hiddenOnly && object.visible) || !matchesText) {
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
