#include "scene/StageDecorationSource.h"

#include "physics/ColliderUtils.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"

#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace {
/// <summary>
/// 風のスクリプトが参照する領域を、速度とワールド境界に変換する
/// </summary>
void ReadWindAreas(const Scene &scene, const ScriptComponent &script,
                   const SceneHierarchy::WorldTransforms &transforms,
                   const SceneActivity::States &states,
                   std::vector<StageDecorationLayout::WindArea> &areas) {
	const auto state = nlohmann::json::parse(script.initialState);
	const auto &fields = state.at("fields");
	if (!fields.is_array()) {
		throw std::runtime_error("Wind script fields must be an array.");
	}

	for (const auto &field : fields) {
		const auto &reference = field.at("reference").get_ref<const std::string &>();
		const auto target = std::ranges::find(script.references, reference,
		                                    &decltype(script.references)::value_type::first);
		const auto velocity = field.at("velocity").get<std::array<float, 3>>();
		if (target == script.references.end() ||
		    !std::ranges::all_of(velocity, [](float value) {
			    return std::isfinite(value);
		    })) {
			throw std::runtime_error("Wind field reference or velocity is invalid.");
		}

		const auto *zone = scene.FindObject(target->second);
		if (!zone) {
			throw std::runtime_error("Wind field object is missing.");
		}
		if (!states.at(target->second)) {
			continue;
		}

		// スクリプトはワールド座標を変換し、ローカルの -1 から 1 までを風の範囲として扱う。
		Collider shape;
		shape.halfExtents = {1.0f, 1.0f, 1.0f};
		AxisAlignedBox bounds;
		if (!ColliderUtils::GetWorldBounds(shape, transforms.at(target->second).matrix,
		                                  bounds)) {
			throw std::runtime_error("Wind field transform is invalid.");
		}
		areas.push_back({bounds, {velocity[0], velocity[1], velocity[2]}});
	}
}
} // namespace

bool StageDecorationSource::Read(const Scene &scene, std::string_view platformPrefix,
                                 const std::filesystem::path &windScript,
                                 std::string &error) {
	try {
		SceneActivity::States states;
		SceneHierarchy::WorldTransforms transforms;
		if (!SceneActivity::BuildStates(scene, states) ||
		    !SceneHierarchy::BuildWorldTransforms(scene, transforms)) {
			throw std::runtime_error("Scene hierarchy or transforms are invalid.");
		}
		if (platformPrefix.empty()) {
			throw std::runtime_error("The decoration theme needs a platform model prefix.");
		}

		StageDecorationSource source;
		for (const auto &[id, object] : scene.GetObjects()) {
			if (!states.at(id)) {
				continue;
			}
			if (!windScript.empty() && object.script && object.script->enabled &&
			    PathUtils::IsSameFile(object.script->asset, windScript)) {
				ReadWindAreas(scene, *object.script, transforms, states, source.windAreas);
			}
			if (!object.collider || object.collider->isTrigger || object.character) {
				continue;
			}

			AxisAlignedBox bounds;
			if (!ColliderUtils::GetWorldBounds(*object.collider, transforms.at(id).matrix,
			                                  bounds)) {
				throw std::runtime_error("Could not calculate collider bounds for " +
				                         object.name + ".");
			}
			if (object.HasMesh() &&
			    PathUtils::ToUtf8(object.mesh->modelPath.stem()).starts_with(platformPrefix)) {
				source.route.push_back(bounds);
			} else {
				source.obstacles.push_back(bounds);
			}
		}

		if (source.route.size() < 2) {
			throw std::runtime_error("The theme needs at least two matching platforms.");
		}

		*this = std::move(source);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
