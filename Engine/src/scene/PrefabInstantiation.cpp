#include "scene/PrefabInstantiation.h"
#include "math/MathUtils.h"
#include "scene/PrefabSerializer.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneObjectReferences.h"
#include "scene/SceneSerializer.h"
#include <map>
#include <stdexcept>
#include <utility>

SceneObjectId
PrefabInstantiation::Instantiate(const Scene &prefab, Scene &scene,
                                 const DirectX::XMFLOAT3 &position,
                                 std::string &error) {
	try {
		SceneObjectId root = 0;

		if (!MathUtils::IsFinite(position)) {
			error = "Prefab placement requires a finite world position.";
			return 0;
		}

		if (!PrefabSerializer::Validate(prefab, root, error))
			return 0;
		if (prefab.GetObjects().size() > kMaximumObjects ||
		    scene.GetObjects().size() >
		        kMaximumObjects - prefab.GetObjects().size())
			throw std::runtime_error(
			    "Prefab instantiation exceeds 4096 scene objects.");
		std::vector<SceneObjectId> order;
		if (!SceneHierarchy::GetEvaluationOrder(prefab, order))
			throw std::runtime_error("Prefab hierarchy is invalid.");
		const auto original = prefab.FindObject(root)->transform.translation;
		const DirectX::XMFLOAT3 delta{position.x - original.x,
		                              position.y - original.y,
		                              position.z - original.z};
		if (!MathUtils::IsFinite(delta))
			throw std::runtime_error(
			    "Prefab placement offset is outside the supported range.");
		SceneObjectId created = 0;

		if (!scene.ApplyTransaction([&](Scene &candidate) {
			    std::map<SceneObjectId, SceneObjectId> remap;

			    for (auto id : order) {
				    auto object = *prefab.FindObject(id);
				    object.parentId =
				        object.parentId ? remap.at(object.parentId) : 0;
				    if (id == root)
					    object.transform.translation = position;
				    if (object.respawn) {
					    auto &r = *object.respawn;
					    r.minimumY += delta.y;
					    r.position = {r.position.x + delta.x,
					                  r.position.y + delta.y,
					                  r.position.z + delta.z};
				    }

				    const auto added =
				        candidate.CreateObject(std::move(object));
				    remap.emplace(id, added);
			    }

			    for (const auto &[source, target] : remap) {
				    (void)source;
				    SceneObjectReferences::Remap(*candidate.FindObject(target),
				                                 remap);
			    }

			    std::string serialized;
			    if (!SceneSerializer::Serialize(candidate, serialized))
				    throw std::runtime_error(
				        "Instantiated Prefab contains invalid settings or "
				        "translated coordinates.");
			    created = remap.at(root);
			    return true;
		    }))
			throw std::runtime_error(
			    "Could not instantiate the Prefab hierarchy.");
		error.clear();
		return created;
	} catch (const std::exception &exception) {
		error = exception.what();
		return 0;
	}
}
