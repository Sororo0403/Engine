#include "camera/ObjectFrameBounds.h"

#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include "physics/BoxColliderUtils.h"
#include "scene/SceneObject.h"

bool ObjectFrameBounds::Get(const SceneObject &object, Corners &corners) {
	if ((object.HasMesh() && object.meshRenderer) ||
	    !TransformUtils::IsValid(object.transform)) {
		return false;
	}

	AxisAlignedBox bounds;

	if (object.collider) {
		if (!BoxColliderUtils::GetWorldBounds(*object.collider,
		                                      object.transform, bounds)) {
			return false;
		}
	} else {
		const auto &position = object.transform.translation;
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
