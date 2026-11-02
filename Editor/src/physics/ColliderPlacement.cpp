#include "physics/ColliderPlacement.h"

#include "physics/BoxColliderUtils.h"
#include "scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <set>

namespace {
/// <summary>
/// 上下移動の経路が交差する、正の面積の重なりを確認する。辺だけの接触は除く
/// </summary>
/// <param name="first">移動するボックス</param>
/// <param name="second">障害物のボックス</param>
/// <returns>true:XZ平面で面積を持つ重なりあり false:重なりなし</returns>
bool OverlapsHorizontally(const AxisAlignedBox &first,
                          const AxisAlignedBox &second) {
	return first.minimum.x < second.maximum.x &&
	       first.maximum.x > second.minimum.x &&
	       first.minimum.z < second.maximum.z &&
	       first.maximum.z > second.minimum.z;
}
} // namespace

bool ColliderPlacement::CanDrop(const Scene &scene,
                                std::span<const SceneObjectId> objectIds) {
	return !objectIds.empty() &&
	       std::all_of(objectIds.begin(), objectIds.end(), [&](auto id) {
		       const auto *object = scene.FindObject(id);
		       return object && object->collider.has_value();
	       });
}

bool ColliderPlacement::GetDropTranslation(
    const Scene &scene, std::span<const SceneObjectId> objectIds,
    DirectX::XMFLOAT3 &translation, std::string &error) {
	error.clear();

	if (!CanDrop(scene, objectIds)) {
		error =
		    "Select objects with Box Colliders before dropping to a surface.";
		return false;
	}

	const std::set<SceneObjectId> selected(objectIds.begin(), objectIds.end());

	if (selected.size() != objectIds.size()) {
		error = "The selection contains duplicate object IDs.";
		return false;
	}

	std::map<SceneObjectId, AxisAlignedBox> bodies, obstacles;

	for (const auto &[id, object] : scene.GetObjects()) {
		if (!object.collider) {
			continue;
		}

		AxisAlignedBox bounds;

		if (!BoxColliderUtils::GetWorldBounds(*object.collider,
		                                      object.transform, bounds)) {
			error =
			    std::format("Invalid Box Collider on object: \"{}\" (ID {}).",
			                object.name.empty() ? "Object" : object.name, id);
			return false;
		}

		(selected.contains(id) ? bodies : obstacles).emplace(id, bounds);
	}

	double distance = (std::numeric_limits<double>::infinity)();

	for (const auto &[id, body] : bodies) {
		for (const auto &[obstacleId, obstacle] : obstacles) {
			if (!OverlapsHorizontally(body, obstacle)) {
				continue;
			}

			if (body.minimum.y < obstacle.maximum.y &&
			    body.maximum.y > obstacle.minimum.y) {
				error = std::format("Object \"{}\" (ID {}) already overlaps "
				                    "Box Collider ID {}.",
				                    scene.FindObject(id)->name, id, obstacleId);
				return false;
			}

			if (obstacle.maximum.y <= body.minimum.y) {
				distance =
				    (std::min)(distance, static_cast<double>(body.minimum.y) -
				                             obstacle.maximum.y);
			}
		}
	}

	if (!std::isfinite(distance)) {
		error = "No Box Collider surface was found below the selection.";
		return false;
	}

	const double safeDistance = (std::max)(0.0, distance - kSurfaceClearance);
	float drop = static_cast<float>(safeDistance);

	if (!std::isfinite(drop)) {
		error = "The drop distance cannot be represented at this position.";
		return false;
	}

	if (static_cast<double>(drop) > safeDistance) {
		drop = std::nextafter(drop, 0.0f);
	}

	// 履歴で実際に行うfloatの加算と形状変換を確認してから、移動量を返す。
	bool movedAny = false;
	for (const auto id : objectIds) {
		const auto &object = *scene.FindObject(id);
		auto transform = object.transform;
		transform.translation.y -= drop;
		movedAny |= transform.translation.y != object.transform.translation.y;
		AxisAlignedBox moved;

		if (!BoxColliderUtils::GetWorldBounds(*object.collider, transform,
		                                      moved)) {
			error =
			    "The dropped collider cannot be represented at this transform.";
			return false;
		}

		for (const auto &[obstacleId, obstacle] : obstacles) {
			if (OverlapsHorizontally(moved, obstacle) &&
			    moved.minimum.y < obstacle.maximum.y &&
			    moved.maximum.y > obstacle.minimum.y) {
				error = "The drop would overlap another collider due to "
				        "coordinate precision.";
				return false;
			}
		}
	}

	translation = {0.0f, movedAny ? -drop : 0.0f, 0.0f};
	return true;
}
