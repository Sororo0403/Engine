#include "physics/ColliderBounds.h"

#include "graphics/render/DebugLine.h"
#include "physics/BoxColliderUtils.h"
#include "scene/SceneObject.h"
#include "scene/SceneRenderer.h"

#include <array>

bool ColliderBounds::Update(SceneRenderer &renderer,
                            const SceneObject &object) const {
	if (!object.collider) {
		return true;
	}

	AxisAlignedBox box;

	if (!BoxColliderUtils::GetWorldBounds(*object.collider, object.transform,
	                                      box)) {
		return true;
	}

	std::array<DirectX::XMFLOAT3, 8> corners;

	for (unsigned int i = 0; i < 8; ++i) {
		corners[i] = {(i & 1) ? box.maximum.x : box.minimum.x,
		              (i & 2) ? box.maximum.y : box.minimum.y,
		              (i & 4) ? box.maximum.z : box.minimum.z};
	}

	for (unsigned int i = 0; i < 8; ++i) {
		for (unsigned int axis : {1u, 2u, 4u}) {
			if ((i & axis) == 0 &&
			    !renderer.AddDebugLine(
			        {corners[i], corners[i | axis], color_})) {
				return false;
			}
		}
	}

	return true;
}
