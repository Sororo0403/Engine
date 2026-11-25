#include "physics/ColliderPlacement.h"
#include "scene/SceneActivity.h"

#include "physics/ColliderUtils.h"
#include "physics/WorldCollider.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <set>

namespace {
/// <summary>
/// 実際の履歴と同じワールド移動を、検証用のシーンへ適用する
/// </summary>
/// <param name="source">変更しない元シーン</param>
/// <param name="ids">選択ID</param>
/// <param name="translation">ワールド移動量</param>
/// <param name="result">試行結果を保持する一時シーン</param>
/// <returns>true:成功 false:選択・変形が無効</returns>
bool MakeMovedScene(const Scene &source, std::span<const SceneObjectId> ids,
                    const DirectX::XMFLOAT3 &translation, Scene &result) {
	result.ReplaceObjects(source.GetObjects());
	return SceneHierarchy::TranslateSelection(result, ids, translation);
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
		error = "Select objects with Colliders before dropping to a surface.";
		return false;
	}

	std::vector<SceneObjectId> expanded;

	if (!SceneHierarchy::ExpandSelection(scene, objectIds, expanded)) {
		error = "The selection or parent hierarchy is invalid.";
		return false;
	}

	const std::set<SceneObjectId> selected(expanded.begin(), expanded.end());

	struct Shape {
		AxisAlignedBox bounds;
		WorldCollider geometry;
	};

	std::map<SceneObjectId, Shape> bodies, obstacles;

	for (const auto &[id, object] : scene.GetObjects()) {
		if (!object.collider ||
		    (!selected.contains(id) && (!SceneActivity::IsActive(scene, id) ||
		                                object.collider->isTrigger)))
			continue;
		SceneWorldTransform transform;
		Shape shape;
		if (!SceneHierarchy::GetWorldTransform(scene, id, transform) ||
		    !WorldCollider::Create(*object.collider, transform.matrix,
		                           shape.geometry, error) ||
		    !shape.geometry.GetBounds(shape.bounds)) {
			error = std::format("Invalid Collider on object ID {}.", id);
			return false;
		}
		(selected.contains(id) ? bodies : obstacles)
		    .emplace(id, std::move(shape));
	}

	double distance = (std::numeric_limits<double>::infinity)();
	for (const auto &[id, body] : bodies)
		for (const auto &[obstacleId, obstacle] : obstacles) {
			bool overlap = false;
			if (!body.geometry.Overlaps(
			        obstacle.geometry,
			        static_cast<float>(kSurfaceClearance * .1), overlap)) {
				error = "Could not test Collider overlap.";
				return false;
			}
			if (overlap) {
				error = std::format(
				    "Collider ID {} already overlaps Collider ID {}.", id,
				    obstacleId);
				return false;
			}

			const double range = double(body.bounds.maximum.y) -
			                     obstacle.bounds.minimum.y + kSurfaceClearance;
			if (range <= 0)
				continue;
			if (range > 2 * WorldCollider::kMaximumCoordinate) {
				error = "Drop is outside the supported range.";
				return false;
			}

			const float length = static_cast<float>(range);
			const DirectX::XMFLOAT3 movement{0, -length, 0};
			std::optional<ColliderCastHit> hit;
			bool crosses = false;

			if (!body.geometry.Cast(obstacle.geometry, movement, hit) ||
			    !body.geometry.Crosses(obstacle.geometry, movement, .000001f,
			                           crosses)) {
				error = "Could not cast the selected Collider.";
				return false;
			}

			if (hit && crosses)
				distance = (std::min)(distance, hit->fraction * length);
		}

	if (!std::isfinite(distance)) {
		error = "No solid Collider surface was found below the selection.";
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
	Scene movedScene;

	if (!MakeMovedScene(scene, objectIds, {0, -drop, 0}, movedScene)) {
		error = "The dropped hierarchy cannot be represented.";
		return false;
	}

	bool movedAny = false;

	for (const auto &[id, body] : bodies) {
		const auto &object = *movedScene.FindObject(id);
		AxisAlignedBox moved;

		if (!ColliderUtils::GetWorldBounds(*object.collider, movedScene, id,
		                                   moved)) {
			error =
			    "The dropped collider cannot be represented at this transform.";
			return false;
		}

		movedAny |= moved.minimum.y != body.bounds.minimum.y;

		SceneWorldTransform transform;
		WorldCollider geometry;
		if (!SceneHierarchy::GetWorldTransform(movedScene, id, transform) ||
		    !WorldCollider::Create(*object.collider, transform.matrix, geometry,
		                           error))
			return false;
		for (const auto &[obstacleId, obstacle] : obstacles) {
			bool overlap = false;
			if (!geometry.Overlaps(obstacle.geometry,
			                       static_cast<float>(kSurfaceClearance * .1),
			                       overlap))
				return false;
			if (overlap) {
				error = "The drop would overlap another collider due to "
				        "coordinate precision.";
				return false;
			}
		}
	}

	translation = {0.0f, movedAny ? -drop : 0.0f, 0.0f};
	return true;
}
