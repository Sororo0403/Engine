#include "gameplay/RespawnPreview.h"

#include "math/MathUtils.h"
#include "physics/BoxColliderUtils.h"
#include "physics/CollisionWorld.h"
#include "scene/Scene.h"
#include "scene/SceneObject.h"
#include "scene/SceneRenderer.h"

bool RespawnPreview::BuildGeometry(const Scene &scene, SceneObjectId id,
                                   Geometry &geometry) const {
	const auto *object = scene.FindObject(id);
	Geometry result;

	if (!object || !BuildLines(*object, result.markers)) {
		return false;
	}

	const auto &settings = *object->respawn;
	auto transform = object->transform;
	transform.translation =
	    settings.useStartPosition ? transform.translation : settings.position;
	AxisAlignedBox body;

	if (object->collider &&
	    BoxColliderUtils::GetWorldBounds(*object->collider, transform, body)) {
		ColliderBounds::Lines outline;
		ColliderBounds bounds;

		if (bounds.BuildLines(body, outline)) {
			result.collider = outline;
		}

		if (object->character && object->character->IsValid() &&
		    transform.translation.y > settings.minimumY) {
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
