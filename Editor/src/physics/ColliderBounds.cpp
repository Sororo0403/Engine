#include "physics/ColliderBounds.h"

#include "graphics/render/DebugLine.h"
#include "physics/BoxColliderUtils.h"
#include "scene/Scene.h"
#include "scene/SceneRenderer.h"

#include <array>

bool ColliderBounds::Update(SceneRenderer &renderer, const Scene &scene,
                            std::span<const SceneObjectId> selected,
                            Mode mode) const {
	switch (mode) {
	case Mode::NONE:
		return true;
	case Mode::SELECTED:
		for (const auto id : selected) {
			if (const auto *object = scene.FindObject(id);
			    object && !Update(renderer, *object)) {
				return false;
			}
		}

		return true;
	case Mode::ALL:
		for (const auto &[id, object] : scene.GetObjects()) {
			if (!Update(renderer, object)) {
				return false;
			}
		}

		return true;
	default:
		return false;
	}
}

bool ColliderBounds::BuildLines(const SceneObject &object, Lines &lines) const {
	if (!object.collider) {
		return false;
	}

	AxisAlignedBox box;

	if (!BoxColliderUtils::GetWorldBounds(*object.collider, object.transform,
	                                      box)) {
		return false;
	}

	std::array<DirectX::XMFLOAT3, 8> corners;

	for (unsigned int i = 0; i < 8; ++i) {
		corners[i] = {(i & 1) ? box.maximum.x : box.minimum.x,
		              (i & 2) ? box.maximum.y : box.minimum.y,
		              (i & 4) ? box.maximum.z : box.minimum.z};
	}

	Lines result;
	std::size_t index = 0;

	for (unsigned int i = 0; i < 8; ++i) {
		for (unsigned int axis : {1u, 2u, 4u}) {
			if ((i & axis) == 0) {
				result[index++] = {corners[i], corners[i | axis], color_};
			}
		}
	}

	lines = result;
	return true;
}

bool ColliderBounds::Update(SceneRenderer &renderer,
                            const SceneObject &object) const {
	Lines lines;

	if (!BuildLines(object, lines)) {
		return true;
	}

	for (const auto &line : lines) {
		if (!renderer.AddDebugLine(line)) {
			return false;
		}
	}

	return true;
}
