#include "gameplay/RespawnPreview.h"

#include "math/MathUtils.h"
#include "physics/BoxColliderUtils.h"
#include "physics/CollisionWorld.h"
#include "scene/Scene.h"
#include "scene/SceneObject.h"
#include "scene/SceneRenderer.h"
#include <algorithm>

bool RespawnPreview::BuildGeometry(const Scene &scene, SceneObjectId id,
                                   Geometry &geometry) const {
	const auto *object = scene.FindObject(id);
	Geometry result;

	if (!object || !BuildLines(*object, result.markers)) {
		return false;
	}

	const auto &settings = *object->respawn;
	AxisAlignedBox body;

	if (GetColliderBounds(*object, body)) {
		ColliderBounds::Lines outline;
		ColliderBounds bounds;

		if (bounds.BuildLines(body, outline)) {
			result.collider = outline;
		}

		if (object->character && object->character->IsValid() &&
		    result.markers[3].start.y > settings.minimumY) {
			CollisionWorld world;
			result.canRespawn =
			    world.Rebuild(scene, id) && world.CanPlace(body);
		}
	}

	const auto color = result.canRespawn ? positionColor_ : invalidColor_;

	// 判定高度の色は変えず、復帰位置の十字と接続線だけに配置可否を反映する。
	for (std::size_t i = 0; i < 4; ++i) {
		result.markers[i].color = color;
	}

	if (result.collider) {
		for (auto &line : *result.collider) {
			line.color = color;
		}
	}

	geometry = result;
	return true;
}

bool RespawnPreview::BuildLines(const SceneObject &object, Lines &lines) const {
	if (!object.respawn || !object.respawn->IsValid()) {
		return false;
	}

	const auto &settings = *object.respawn;
	const auto position = settings.useStartPosition
	                          ? object.transform.translation
	                          : settings.position;

	if (!MathUtils::IsFinite(position)) {
		return false;
	}

	const DirectX::XMFLOAT3 below{position.x, settings.minimumY, position.z};
	const std::array<DirectX::XMFLOAT3, 4> corners{{
	    {position.x - kFallPlaneHalfExtent, settings.minimumY,
	     position.z - kFallPlaneHalfExtent},
	    {position.x + kFallPlaneHalfExtent, settings.minimumY,
	     position.z - kFallPlaneHalfExtent},
	    {position.x + kFallPlaneHalfExtent, settings.minimumY,
	     position.z + kFallPlaneHalfExtent},
	    {position.x - kFallPlaneHalfExtent, settings.minimumY,
	     position.z + kFallPlaneHalfExtent},
	}};
	Lines result{};
	result[0] = {{position.x - kMarkerRadius, position.y, position.z},
	             {position.x + kMarkerRadius, position.y, position.z},
	             positionColor_};
	result[1] = {{position.x, position.y - kMarkerRadius, position.z},
	             {position.x, position.y + kMarkerRadius, position.z},
	             positionColor_};
	result[2] = {{position.x, position.y, position.z - kMarkerRadius},
	             {position.x, position.y, position.z + kMarkerRadius},
	             positionColor_};
	result[3] = {position, below, positionColor_};

	for (std::size_t i = 0; i < corners.size(); ++i) {
		result[4 + i] = {corners[i], corners[(i + 1) % corners.size()],
		                 fallColor_};
	}

	result[8] = {corners[0], corners[2], fallColor_};
	result[9] = {corners[1], corners[3], fallColor_};
	lines = result;
	return true;
}

bool RespawnPreview::GetFrameBounds(const SceneObject &object,
                                    std::array<DirectX::XMFLOAT3, 8> &corners) {
	if (!object.respawn || !object.respawn->IsValid()) {
		return false;
	}

	auto transform = object.transform;
	transform.translation = object.respawn->useStartPosition
	                            ? transform.translation
	                            : object.respawn->position;
	const auto &position = transform.translation;

	if (!MathUtils::IsFinite(position)) {
		return false;
	}

	AxisAlignedBox box{{position.x - kMarkerRadius, position.y - kMarkerRadius,
	                    position.z - kMarkerRadius},
	                   {position.x + kMarkerRadius, position.y + kMarkerRadius,
	                    position.z + kMarkerRadius}};
	AxisAlignedBox body;

	if (GetColliderBounds(object, body)) {
		box.minimum = {(std::min)(box.minimum.x, body.minimum.x),
		               (std::min)(box.minimum.y, body.minimum.y),
		               (std::min)(box.minimum.z, body.minimum.z)};
		box.maximum = {(std::max)(box.maximum.x, body.maximum.x),
		               (std::max)(box.maximum.y, body.maximum.y),
		               (std::max)(box.maximum.z, body.maximum.z)};
	}

	if (!MathUtils::IsFinite(box.minimum) ||
	    !MathUtils::IsFinite(box.maximum) || box.minimum.x >= box.maximum.x ||
	    box.minimum.y >= box.maximum.y || box.minimum.z >= box.maximum.z) {
		return false;
	}

	std::array<DirectX::XMFLOAT3, 8> result;

	for (std::size_t i = 0; i < result.size(); ++i) {
		result[i] = {(i & 1) ? box.maximum.x : box.minimum.x,
		             (i & 2) ? box.maximum.y : box.minimum.y,
		             (i & 4) ? box.maximum.z : box.minimum.z};
	}

	corners = result;
	return true;
}

bool RespawnPreview::GetColliderBounds(const SceneObject &object,
                                       AxisAlignedBox &bounds) {
	if (!object.respawn || !object.respawn->IsValid() || !object.collider) {
		return false;
	}

	auto transform = object.transform;
	transform.translation = object.respawn->useStartPosition
	                            ? transform.translation
	                            : object.respawn->position;

	return BoxColliderUtils::GetWorldBounds(*object.collider, transform,
	                                        bounds);
}

bool RespawnPreview::FindBlockingObjects(const Scene &scene, SceneObjectId id,
                                         std::vector<SceneObjectId> &objects) {
	const auto *object = scene.FindObject(id);
	AxisAlignedBox body;

	return object && GetColliderBounds(*object, body) &&
	       CollisionWorld::FindOverlaps(scene, body, objects, id);
}

bool RespawnPreview::Update(SceneRenderer &renderer, const Scene &scene,
                            SceneObjectId id) const {
	Geometry geometry;

	if (!BuildGeometry(scene, id, geometry)) {
		return true;
	}

	for (const auto &line : geometry.markers) {
		if (!renderer.AddDebugLine(line)) {
			return false;
		}
	}

	if (geometry.collider) {
		for (const auto &line : *geometry.collider) {
			if (!renderer.AddDebugLine(line)) {
				return false;
			}
		}
	}

	return true;
}
