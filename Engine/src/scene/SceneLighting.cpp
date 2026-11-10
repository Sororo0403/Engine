#include "scene/SceneLighting.h"

#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include <algorithm>
#include <cmath>
#include <format>

SceneObject SceneLighting::CreateDirectionalLight(const LightingSettings &settings) {
	SceneObject sun;
	sun.name = "Directional Light";
	sun.directionalLight = DirectionalLightComponent{
	    {settings.color.x, settings.color.y, settings.color.z},
	    settings.intensity, settings.shadows};

	const auto &direction = settings.direction;
	const float yaw = std::atan2(direction.x, direction.z);
	const float pitch = static_cast<float>(std::atan2(
	    -double(direction.y), std::hypot(double(direction.x), double(direction.z))));
	DirectX::XMStoreFloat4(&sun.transform.rotation,
	                      DirectX::XMQuaternionRotationRollPitchYaw(pitch, yaw, 0));
	return sun;
}

void SceneLighting::MigrateLegacyDirectionalLight(
    Scene &scene, const LightingSettings &fallback) {
	if (!scene.UsesLegacyDirectionalLight())
		return;

	const bool hasLight = std::ranges::any_of(
	    scene.GetObjects(), [](const auto &entry) {
		    return entry.second.directionalLight.has_value();
	    });

	if (!hasLight) {
		scene.CreateObject(
		    CreateDirectionalLight(scene.GetLighting().value_or(fallback)));
	}

	scene.SetUseLegacyDirectionalLight(false);
}

bool SceneLighting::CollectDirectionalLights(
    const Scene &scene, const SceneHierarchy::WorldTransforms &transforms,
    const SceneActivity::States &active, const LightingSettings &fallback,
    DirectionalLightsData &lights, ShadowSettings &shadows, std::string &error,
    SceneObjectId *invalidObject) {
	error.clear();
	if (invalidObject)
		*invalidObject = 0;

	const auto settings = scene.GetLighting().value_or(fallback);
	DirectionalLightsData result;
	result.ambient = settings.ambient;
	result.count = 0;
	ShadowSettings shadow;

	for (const auto &[id, object] : scene.GetObjects()) {
		if (!object.directionalLight)
			continue;

		const auto &light = *object.directionalLight;
		if (!active.at(id))
			continue;

		if (!light.IsValid() || result.count == result.lights.size()) {
			error = !light.IsValid()
			            ? "Directional Light settings are invalid."
			            : std::format("At most {} Directional Lights can be active at once.",
			                          result.lights.size());
			if (invalidObject)
				*invalidObject = id;
			return false;
		}

		DirectX::XMFLOAT3 direction;
		DirectX::XMStoreFloat3(
		    &direction, DirectX::XMVector3Rotate(
		                    DirectX::XMVectorSet(0, 0, 1, 0),
		                    DirectX::XMLoadFloat4(&transforms.at(id).rotation)));
		if (light.shadows.enabled && result.shadowIndex < 0) {
			result.shadowIndex = static_cast<std::int32_t>(result.count);
			shadow = light.shadows;
		}

		result.lights[result.count++] = {
		    direction, light.intensity, {light.color[0], light.color[1], light.color[2]}};
	}


	lights = result;
	shadows = shadow;
	return true;
}

bool SceneLighting::CollectPointLights(const Scene &scene,
                                       const SceneHierarchy::WorldTransforms &transforms,
                                       const SceneActivity::States &active,
                                       std::vector<PointLightData> &lights,
                                       std::string &error,
                                       SceneObjectId *invalidObject) {
	error.clear();

	if (invalidObject) {
		*invalidObject = 0;
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

bool SceneLighting::CollectSpotLights(const Scene &scene,
                                      const SceneHierarchy::WorldTransforms &transforms,
                                      const SceneActivity::States &active,
                                      std::vector<SpotLightData> &lights,
                                      std::string &error,
                                      SceneObjectId *invalidObject) {
	error.clear();

	if (invalidObject) {
		*invalidObject = 0;
	}

	std::vector<SpotLightData> result;

	for (const auto &[id, object] : scene.GetObjects()) {
		if (!object.spotLight || !active.at(id)) {
			continue;
		}

		if (!object.spotLight->IsValid() ||
		    result.size() >= SpotLightData::kMaximumLights) {
			error = !object.spotLight->IsValid()
			            ? "Spot Light settings are invalid."
			            : std::format(
			                  "At most {} Spot Lights can be active at once.",
			                  SpotLightData::kMaximumLights);
			if (invalidObject) {
				*invalidObject = id;
			}

			return false;
		}

		const auto &value = *object.spotLight;
		const auto &world = transforms.at(id);
		DirectX::XMFLOAT3 direction;
		DirectX::XMStoreFloat3(
		    &direction, DirectX::XMVector3Normalize(DirectX::XMVector3Rotate(
		                    DirectX::XMVectorSet(0, 0, 1, 0),
		                    DirectX::XMLoadFloat4(&world.rotation))));
		SpotLightData light{
		    world.GetPosition(),
		    value.range,
		    direction,
		    std::cos(DirectX::XMConvertToRadians(value.innerAngle * .5f)),
		    {value.color[0], value.color[1], value.color[2]},
		    value.intensity,
		    std::cos(DirectX::XMConvertToRadians(value.outerAngle * .5f))};
		if (!light.IsValid()) {
			error = "Spot Light world position or direction is invalid.";

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
