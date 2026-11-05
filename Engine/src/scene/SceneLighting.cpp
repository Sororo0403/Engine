#include "scene/SceneLighting.h"

#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include <format>

bool SceneLighting::CollectPointLights(const Scene &scene,
                                       std::vector<PointLightData> &lights,
                                       std::string &error,
                                       SceneObjectId *invalidObject) {
	error.clear();
	if (invalidObject) {
		*invalidObject = 0;
	}
	SceneHierarchy::WorldTransforms transforms;
	SceneActivity::States active;
	if (!SceneHierarchy::BuildWorldTransforms(scene, transforms) ||
	    !SceneActivity::BuildStates(scene, active)) {
		error = "Point Light hierarchy or world transform is invalid.";
		return false;
	}
	std::vector<PointLightData> result;
	for (const auto &[id, object] : scene.GetObjects()) {
		if (!object.pointLight || !active.at(id)) {
			continue;
		}
		if (!object.pointLight->IsValid() ||
		    result.size() >= PointLightData::kMaximumLights) {
			error = !object.pointLight->IsValid()
			            ? "Point Light settings are invalid."
			            : std::format(
			                  "At most {} Point Lights can be active at once.",
			                  PointLightData::kMaximumLights);
			if (invalidObject) {
				*invalidObject = id;
			}
			return false;
		}
		const auto &value = *object.pointLight;
		PointLightData light{transforms.at(id).GetPosition(),
		                     value.range,
		                     {value.color[0], value.color[1], value.color[2]},
		                     value.intensity};
		if (!light.IsValid()) {
			error = "Point Light world position is invalid.";
			if (invalidObject) {
				*invalidObject = id;
			}
			return false;
		}
		result.push_back(light);
	}
	lights.swap(result);
	return true;
}
