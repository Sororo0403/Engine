#include "gameplay/RespawnPreview.h"
#include "scene/SceneActivity.h"

#include "math/MathUtils.h"
#include "physics/ColliderUtils.h"
#include "physics/CollisionWorld.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneObject.h"
#include "scene/SceneRenderer.h"
#include <algorithm>

namespace {
/// <summary>
/// 復帰先でのColliderのワールド姿勢を求める
/// </summary>
bool GetRespawnPose(const Scene &scene, SceneObjectId id,
                    SceneWorldTransform &world) {
	const auto *object = scene.FindObject(id);
	if (!object || !object->respawn || !object->respawn->IsValid() ||
	    !object->collider ||
	    !SceneHierarchy::GetWorldTransform(scene, id, world)) {
		return false;
	}

	if (!object->respawn->useStartPosition) {
		const auto position = object->respawn->position;
		world.matrix._41 = position.x;
		world.matrix._42 = position.y;
		world.matrix._43 = position.z;
	}

	return true;
}
} // namespace

bool RespawnPreview::BuildGeometry(const Scene &scene, SceneObjectId id,
                                   Geometry &geometry) const {
	const auto *object = scene.FindObject(id);
	Geometry result;

	if (!object || !BuildLines(scene, id, result.markers)) {
		return false;
	}

	const auto &settings = *object->respawn;
	SceneWorldTransform pose;

	if (GetRespawnPose(scene, id, pose)) {
		ColliderBounds bounds;
		std::vector<DebugLine> outline;

		if (object->collider->shape == Collider::Shape::BOX) {
			AxisAlignedBox body;
			ColliderBounds::Lines boxOutline;
			if (ColliderUtils::GetWorldBounds(*object->collider, pose.matrix,
			                                  body) &&
			    bounds.BuildLines(body, boxOutline))
				outline.assign(boxOutline.begin(), boxOutline.end());
		} else {
			bounds.BuildLines(*object->collider, pose.matrix, outline);
		}

		if (!outline.empty())
			result.collider = std::move(outline);

		if (SceneActivity::IsActive(scene, id) && object->character &&
		    object->character->IsValid() && !object->collider->isTrigger &&
		    settings.IsSafe(result.markers[3].start)) {
			CollisionWorld world;
			std::vector<SceneObjectId> ignored;
			result.canRespawn =
			    SceneHierarchy::GetSubtree(scene, id, ignored) &&
			    world.Rebuild(scene, std::span<const SceneObjectId>{ignored}) &&
			    world.CanPlace(*object->collider, pose.matrix);
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
	if (object.parentId != 0 || !object.respawn || !object.respawn->IsValid()) {
		return false;
	}

	const auto position = object.respawn->useStartPosition
	                          ? object.transform.translation
	                          : object.respawn->position;
	return BuildLines(*object.respawn, position, lines);
}

bool RespawnPreview::BuildLines(const Scene &scene, SceneObjectId id,
                                Lines &lines) const {
	const auto *object = scene.FindObject(id);
	SceneWorldTransform world;

	if (!object || !object->respawn || !object->respawn->IsValid() ||
	    !SceneHierarchy::GetWorldTransform(scene, id, world)) {
		return false;
	}

	const auto position = object->respawn->useStartPosition
	                          ? world.GetPosition()
	                          : object->respawn->position;
	return BuildLines(*object->respawn, position, lines);
}

bool RespawnPreview::BuildLines(const RespawnSettings &settings,
                                const DirectX::XMFLOAT3 &position,
                                Lines &lines) const {

	if (!MathUtils::IsFinite(position)) {
		return false;
	}

	const auto boundary = settings.OnBoundary(position);
	const auto corner = [&](float u, float v) {
		auto point = boundary;
		if (settings.axis == RespawnSettings::Axis::X) {
			point.y += u;
			point.z += v;
		} else if (settings.axis == RespawnSettings::Axis::Z) {
			point.x += u;
			point.y += v;
		} else {
			point.x += u;
			point.z += v;
		}
		return point;
	};
	const float extent = kFallPlaneHalfExtent;
	const std::array corners{corner(-extent, -extent), corner(extent, -extent),
	                         corner(extent, extent), corner(-extent, extent)};
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
	result[3] = {position, boundary, positionColor_};

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
