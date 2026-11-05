#include "physics/ColliderBounds.h"
#include "scene/SceneActivity.h"

#include "graphics/render/DebugLine.h"
#include "math/MathUtils.h"
#include "physics/BoxColliderUtils.h"
#include "scene/Scene.h"
#include "scene/SceneRenderer.h"

#include <array>

namespace {
/// <summary>
/// 有効なボックスを、最小・最大座標の組み合わせによる8頂点へ変換する
/// </summary>
/// <param name="box">変換するワールド座標のボックス</param>
/// <param name="corners">成功時に置き換える頂点</param>
/// <returns>true:成功 false:座標や大きさが無効</returns>
bool GetCorners(const AxisAlignedBox &box,
                std::array<DirectX::XMFLOAT3, 8> &corners) {
	if (!MathUtils::IsFinite(box.minimum) ||
	    !MathUtils::IsFinite(box.maximum) || box.minimum.x >= box.maximum.x ||
	    box.minimum.y >= box.maximum.y || box.minimum.z >= box.maximum.z) {
		return false;
	}

	std::array<DirectX::XMFLOAT3, 8> result;

	for (unsigned int i = 0; i < 8; ++i) {
		result[i] = {(i & 1) ? box.maximum.x : box.minimum.x,
		             (i & 2) ? box.maximum.y : box.minimum.y,
		             (i & 4) ? box.maximum.z : box.minimum.z};
	}

	corners = result;
	return true;
}
} // namespace

bool ColliderBounds::GetFrameBounds(const SceneObject &object,
                                    std::array<DirectX::XMFLOAT3, 8> &corners) {
	AxisAlignedBox box;

	return object.parentId == 0 && object.collider &&
	       BoxColliderUtils::GetWorldBounds(*object.collider, object.transform,
	                                        box) &&
	       GetCorners(box, corners);
}

bool ColliderBounds::Update(SceneRenderer &renderer, const Scene &scene,
                            std::span<const SceneObjectId> selected,
                            Mode mode) const {
	switch (mode) {
	case Mode::NONE:
		return true;
	case Mode::SELECTED:
		for (const auto id : selected) {
			if (const auto *object = scene.FindObject(id);
			    object && !Update(renderer, scene, id)) {
				return false;
			}
		}

		return true;
	case Mode::ALL:
		for (const auto &[id, object] : scene.GetObjects()) {
			if (!SceneActivity::IsActive(scene, id)) {
				continue;
			}
			if (!Update(renderer, scene, id)) {
				return false;
			}
		}

		return true;
	default:
		return false;
	}
}

bool ColliderBounds::BuildLines(const SceneObject &object, Lines &lines) const {
	if (object.parentId != 0 || !object.collider) {
		return false;
	}

	AxisAlignedBox box;

	if (!BoxColliderUtils::GetWorldBounds(*object.collider, object.transform,
	                                      box)) {
		return false;
	}

	return BuildLines(box, lines);
}

bool ColliderBounds::BuildLines(const AxisAlignedBox &box, Lines &lines) const {
	std::array<DirectX::XMFLOAT3, 8> corners;

	if (!GetCorners(box, corners)) {
		return false;
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

bool ColliderBounds::GetFrameBounds(const Scene &scene, SceneObjectId id,
                                    std::array<DirectX::XMFLOAT3, 8> &corners) {
	const auto *object = scene.FindObject(id);
	AxisAlignedBox bounds;
	return object && object->collider &&
	       BoxColliderUtils::GetWorldBounds(*object->collider, scene, id,
	                                        bounds) &&
	       GetCorners(bounds, corners);
}

bool ColliderBounds::Update(SceneRenderer &renderer, const Scene &scene,
                            SceneObjectId id) const {
	const auto *object = scene.FindObject(id);
	if (!object)
		return false;
	if (!object->collider)
		return true;
	AxisAlignedBox bounds;
	Lines lines;
	if (!BoxColliderUtils::GetWorldBounds(*object->collider, scene, id,
	                                      bounds) ||
	    !BuildLines(bounds, lines))
		return false;
	for (const auto &line : lines)
		if (!renderer.AddDebugLine(line))
			return false;
	return true;
}
