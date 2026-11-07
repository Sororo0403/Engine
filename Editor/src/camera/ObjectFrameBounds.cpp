#include "camera/ObjectFrameBounds.h"

#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include "physics/ColliderUtils.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneObject.h"

bool ObjectFrameBounds::Get(const SceneObject &object, Corners &corners,
                            bool missingModel) {
	if (object.parentId != 0 || !TransformUtils::IsValid(object.transform))
		return false;
	DirectX::XMFLOAT4X4 world;
	DirectX::XMStoreFloat4x4(&world,
	                         TransformUtils::MakeWorldMatrix(object.transform));
	return Get(object, world, corners, missingModel);
}

bool ObjectFrameBounds::Get(const Scene &scene, SceneObjectId id,
                            Corners &corners, bool missingModel) {
	const auto *object = scene.FindObject(id);
	SceneWorldTransform world;
	return object && SceneHierarchy::GetWorldTransform(scene, id, world) &&
	       Get(*object, world.matrix, corners, missingModel);
}

bool ObjectFrameBounds::Get(const SceneObject &object,
                            const DirectX::XMFLOAT4X4 &world, Corners &corners,
                            bool missingModel) {
	if ((object.HasMesh() && object.meshRenderer && !missingModel) ||
	    !TransformUtils::IsValid(object.transform)) {
		return false;
	}

	AxisAlignedBox bounds;

	if (object.collider) {
		if (!ColliderUtils::GetWorldBounds(*object.collider, world, bounds)) {
			return false;
		}
	} else {
		const DirectX::XMFLOAT3 position{world._41, world._42, world._43};
		bounds.minimum = {position.x - kOriginHalfExtent,
		                  position.y - kOriginHalfExtent,
		                  position.z - kOriginHalfExtent};
		bounds.maximum = {position.x + kOriginHalfExtent,
		                  position.y + kOriginHalfExtent,
		                  position.z + kOriginHalfExtent};

		if (!MathUtils::IsFinite(bounds.minimum) ||
		    !MathUtils::IsFinite(bounds.maximum) ||
		    bounds.minimum.x >= bounds.maximum.x ||
		    bounds.minimum.y >= bounds.maximum.y ||
		    bounds.minimum.z >= bounds.maximum.z) {
			return false;
		}
	}

	Corners result;

	for (std::size_t i = 0; i < result.size(); ++i) {
		result[i] = {(i & 1) ? bounds.maximum.x : bounds.minimum.x,
		             (i & 2) ? bounds.maximum.y : bounds.minimum.y,
		             (i & 4) ? bounds.maximum.z : bounds.minimum.z};
	}

	corners = result;
	return true;
}
