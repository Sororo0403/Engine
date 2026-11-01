#include "selection/SelectionBounds.h"

#include "graphics/render/DebugLine.h"
#include "scene/SceneRenderer.h"

bool SelectionBounds::Update(SceneRenderer &renderer,
                             SceneObjectId objectId) const {
	if (objectId == 0) {
		return true;
	}

	std::array<DirectX::XMFLOAT3, 8> corners{};

	// モデルを持たない空の物体には表示しない
	if (!renderer.GetObjectBounds(objectId, corners)) {
		return true;
	}

	for (const auto &edge : kEdges) {
		const DebugLine line{
		    corners[edge[0]],
		    corners[edge[1]],
		    color_,
		};

		if (!renderer.AddDebugLine(line)) {
			return false;
		}
	}

	return true;
}
