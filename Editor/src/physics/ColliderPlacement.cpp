#include "physics/ColliderPlacement.h"

#include "history/ObjectArraySettings.h"
#include "physics/BoxColliderUtils.h"
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

/// <summary>
/// 選択枝の全子孫から、衝突形状を持つ物体を重複なく列挙する
/// </summary>
/// <param name="scene">変更しないシーン</param>
/// <param name="ids">選択ID</param>
/// <param name="colliders">成功時だけ置き換えるCollider付きのID一覧</param>
/// <returns>true:形状あり false:選択・親子関係が無効、または形状なし</returns>
bool CollectBranchColliders(const Scene &scene,
                            std::span<const SceneObjectId> ids,
                            std::vector<SceneObjectId> &colliders) {
	std::vector<SceneObjectId> expanded;
	if (!SceneHierarchy::ExpandSelection(scene, ids, expanded)) {
		return false;
	}

	std::erase_if(expanded, [&](SceneObjectId id) {
		return !scene.FindObject(id)->collider;
	});
	if (expanded.empty()) {
		return false;
	}

	colliders.swap(expanded);
	return true;
}

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
		error =
		    "Select objects with Box Colliders before dropping to a surface.";
		return false;
	}

	std::vector<SceneObjectId> expanded;
	if (!SceneHierarchy::ExpandSelection(scene, objectIds, expanded)) {
		error = "The selection or parent hierarchy is invalid.";
		return false;
	}
	const std::set<SceneObjectId> selected(expanded.begin(), expanded.end());

	std::map<SceneObjectId, AxisAlignedBox> bodies, obstacles;

	for (const auto &[id, object] : scene.GetObjects()) {
		if (!object.collider) {
			continue;
		}

		AxisAlignedBox bounds;

		if (!BoxColliderUtils::GetWorldBounds(*object.collider, scene, id,
		                                      bounds)) {
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
	Scene movedScene;
	if (!MakeMovedScene(scene, objectIds, {0, -drop, 0}, movedScene)) {
		error = "The dropped hierarchy cannot be represented.";
		return false;
	}

	bool movedAny = false;
	for (const auto &[id, body] : bodies) {
		const auto &object = *movedScene.FindObject(id);
		AxisAlignedBox moved;
		if (!BoxColliderUtils::GetWorldBounds(*object.collider, movedScene, id,
		                                      moved)) {
			error =
			    "The dropped collider cannot be represented at this transform.";
			return false;
		}

		movedAny |= moved.minimum.y != body.minimum.y;

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

bool ColliderPlacement::GetPlaneTranslation(
    const Scene &scene, std::span<const SceneObjectId> objectIds, float height,
    DirectX::XMFLOAT3 &translation, std::string &error) {
	error.clear();

	if (!std::isfinite(height) || !CanDrop(scene, objectIds)) {
		error =
		    "Select objects with Box Colliders and a finite work-plane height.";
		return false;
	}

	std::vector<SceneObjectId> colliderIds;
	if (!CollectBranchColliders(scene, objectIds, colliderIds)) {
		error = "The selection or parent hierarchy is invalid.";
		return false;
	}

	std::set<SceneObjectId> seen;
	double bottom = (std::numeric_limits<double>::infinity)();

	for (const auto id : colliderIds) {
		const auto &object = *scene.FindObject(id);
		AxisAlignedBox bounds;

		if (!seen.insert(id).second ||
		    !BoxColliderUtils::GetWorldBounds(*object.collider, scene, id,
		                                      bounds)) {
			error = "The selection contains duplicate IDs or invalid Collider "
			        "bounds.";
			return false;
		}

		bottom = (std::min)(bottom, double(bounds.minimum.y));
	}

	const double tolerance =
	    (std::max)(kSurfaceClearance * 2,
	               kFaceRoundoffFactor * std::numeric_limits<float>::epsilon() *
	                   (std::max)(1.0, std::abs(double(height))));

	if (bottom >= height && bottom - height <= tolerance) {
		translation = {};
		return true;
	}

	const double distance = double(height) + kSurfaceClearance - bottom;

	if (!std::isfinite(distance) ||
	    std::abs(distance) > (std::numeric_limits<float>::max)()) {
		error = "The work-plane move cannot be represented at this position.";
		return false;
	}

	const float offset = static_cast<float>(distance);
	double movedBottom = (std::numeric_limits<double>::infinity)();

	Scene movedScene;
	if (!MakeMovedScene(scene, objectIds, {0, offset, 0}, movedScene)) {
		error = "The moved hierarchy cannot be represented.";
		return false;
	}

	for (const auto id : colliderIds) {
		const auto &object = *movedScene.FindObject(id);
		AxisAlignedBox bounds;
		if (!BoxColliderUtils::GetWorldBounds(*object.collider, movedScene, id,
		                                      bounds)) {
			error =
			    "The moved Collider cannot be represented at this transform.";
			return false;
		}

		movedBottom = (std::min)(movedBottom, double(bounds.minimum.y));
	}

	if (movedBottom < height || movedBottom - height > tolerance) {
		error = "Coordinate precision prevents placing the Colliders on the "
		        "work plane.";
		return false;
	}

	translation = {0.0f, offset, 0.0f};
	return true;
}

bool ColliderPlacement::GetAdjacentTranslation(
    const Scene &scene, std::span<const SceneObjectId> objectIds,
    SceneObjectId referenceId, Face face, DirectX::XMFLOAT3 &translation,
    std::string &error) {
	error.clear();
	std::size_t axis = 0;
	bool positive = true;

	switch (face) {
	case Face::RIGHT:
		break;
	case Face::LEFT:
		positive = false;
		break;
	case Face::ABOVE:
		axis = 1;
		break;
	case Face::BELOW:
		axis = 1;
		positive = false;
		break;
	case Face::FRONT:
		axis = 2;
		break;
	case Face::BACK:
		axis = 2;
		positive = false;
		break;
	default:
		error = "Choose a valid collider face.";
		return false;
	}

	if (objectIds.size() < 2 || !CanDrop(scene, objectIds)) {
		error = "Select at least two objects with Box Colliders.";
		return false;
	}

	const std::set<SceneObjectId> selected(objectIds.begin(), objectIds.end());

	if (selected.size() != objectIds.size() ||
	    !selected.contains(referenceId)) {
		error = "The active collider must be included once in the selection.";
		return false;
	}

	std::vector<SceneObjectId> moving, colliderIds;
	for (const auto id : objectIds) {
		if (id != referenceId)
			moving.push_back(id);
	}
	if (!CollectBranchColliders(scene, moving, colliderIds) ||
	    std::find(colliderIds.begin(), colliderIds.end(), referenceId) !=
	        colliderIds.end()) {
		error =
		    "The active reference cannot be a descendant of a moving object.";
		return false;
	}
	colliderIds.insert(colliderIds.begin(), referenceId);

	const auto component = [axis](const DirectX::XMFLOAT3 &value) -> double {
		return axis == 0 ? value.x : axis == 1 ? value.y : value.z;
	};
	double referenceFace = 0;
	double movingFace = positive ? (std::numeric_limits<double>::infinity)()
	                             : -(std::numeric_limits<double>::infinity)();

	for (const auto id : colliderIds) {
		const auto &object = *scene.FindObject(id);
		AxisAlignedBox bounds;

		if (!BoxColliderUtils::GetWorldBounds(*object.collider, scene, id,
		                                      bounds)) {
			error =
			    std::format("Invalid Box Collider on object: \"{}\" (ID {}).",
			                object.name, id);
			return false;
		}

		if (id == referenceId) {
			referenceFace =
			    component(positive ? bounds.maximum : bounds.minimum);
		} else {
			movingFace =
			    positive ? (std::min)(movingFace, component(bounds.minimum))
			             : (std::max)(movingFace, component(bounds.maximum));
		}
	}

	// すでに接している面では、丸め誤差だけの再配置で履歴を増やさない。
	const double tolerance =
	    kSurfaceClearance +
	    kFaceRoundoffFactor * std::numeric_limits<float>::epsilon() *
	        (std::max)({1.0, std::abs(referenceFace), std::abs(movingFace)});
	const double gap =
	    positive ? movingFace - referenceFace : referenceFace - movingFace;

	if (gap >= 0.0 && gap <= tolerance) {
		translation = {};
		return true;
	}

	const double distance = referenceFace - movingFace +
	                        (positive ? kSurfaceClearance : -kSurfaceClearance);

	if (!std::isfinite(distance) ||
	    std::abs(distance) > (std::numeric_limits<float>::max)()) {
		error = "The collider placement distance cannot be represented.";
		return false;
	}

	DirectX::XMFLOAT3 movement{};

	if (axis == 0) {
		movement.x = static_cast<float>(distance);
	} else if (axis == 1) {
		movement.y = static_cast<float>(distance);
	} else {
		movement.z = static_cast<float>(distance);
	}

	// 履歴と同じfloat加算で、配置後の全Colliderが基準の指定側に収まることを確認する。
	double placedFace = positive ? (std::numeric_limits<double>::infinity)()
	                             : -(std::numeric_limits<double>::infinity)();

	Scene movedScene;
	if (!MakeMovedScene(scene, moving, movement, movedScene)) {
		error = "The moving hierarchy cannot be represented.";
		return false;
	}

	for (const auto id : colliderIds) {
		if (id == referenceId)
			continue;
		const auto &object = *movedScene.FindObject(id);
		AxisAlignedBox bounds;
		if (!BoxColliderUtils::GetWorldBounds(*object.collider, movedScene, id,
		                                      bounds) ||
		    (positive ? component(bounds.minimum) < referenceFace
		              : component(bounds.maximum) > referenceFace)) {
			error = "The collider face placement cannot be represented at "
			        "these transforms.";
			return false;
		}
		placedFace = positive
		                 ? (std::min)(placedFace, component(bounds.minimum))
		                 : (std::max)(placedFace, component(bounds.maximum));
	}

	const double finalGap =
	    positive ? placedFace - referenceFace : referenceFace - placedFace;
	const double finalTolerance =
	    kSurfaceClearance +
	    kFaceRoundoffFactor * std::numeric_limits<float>::epsilon() *
	        (std::max)({1.0, std::abs(referenceFace), std::abs(placedFace)});

	if (finalGap > finalTolerance) {
		error =
		    "The collider face cannot be aligned at this coordinate precision.";
		return false;
	}

	translation = movement;
	return true;
}

bool ColliderPlacement::GetRepeatTranslation(
    const Scene &scene, std::span<const SceneObjectId> objectIds, Face face,
    int copies, DirectX::XMFLOAT3 &translation, std::string &error,
    double minimumGap) {
	error.clear();
	if (!std::isfinite(minimumGap) || minimumGap < 0.0) {
		error = "Use a finite, nonnegative minimum gap.";
		return false;
	}
	ObjectArraySettings settings;
	settings.copies = copies;
	if (!settings.IsValid(objectIds.size()) || !CanDrop(scene, objectIds)) {
		error = "Select Box Colliders and a valid number of copies.";
		return false;
	}
	std::vector<SceneObjectId> colliderIds, expanded, roots;
	if (!CollectBranchColliders(scene, objectIds, colliderIds) ||
	    !SceneHierarchy::ExpandSelection(scene, objectIds, expanded) ||
	    !SceneHierarchy::GetSelectionRoots(scene, objectIds, roots) ||
	    !settings.IsValid(expanded.size())) {
		error = "The selection hierarchy or number of copies is invalid.";
		return false;
	}

	std::size_t axis = 0;
	bool positive = true;
	switch (face) {
	case Face::RIGHT:
		break;
	case Face::LEFT:
		positive = false;
		break;
	case Face::ABOVE:
		axis = 1;
		break;
	case Face::BELOW:
		axis = 1;
		positive = false;
		break;
	case Face::FRONT:
		axis = 2;
		break;
	case Face::BACK:
		axis = 2;
		positive = false;
		break;
	default:
		error = "Choose a valid spacing direction.";
		return false;
	}
	const auto component = [axis](const DirectX::XMFLOAT3 &v) {
		return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
	};
	const auto assign = [axis](DirectX::XMFLOAT3 &v, float value) {
		if (axis == 0) {
			v.x = value;
		} else if (axis == 1) {
			v.y = value;
		} else {
			v.z = value;
		}
	};
	std::set<SceneObjectId> seen;
	double minimum = (std::numeric_limits<double>::infinity)();
	double maximum = -minimum;
	for (const auto id : colliderIds) {
		const auto &object = *scene.FindObject(id);
		AxisAlignedBox bounds;
		if (!seen.insert(id).second ||
		    !BoxColliderUtils::GetWorldBounds(*object.collider, scene, id,
		                                      bounds)) {
			error = "The selection contains duplicate IDs or an invalid Box "
			        "Collider.";
			return false;
		}
		minimum = (std::min)(minimum, double(component(bounds.minimum)));
		maximum = (std::max)(maximum, double(component(bounds.maximum)));
	}
	const double width = maximum - minimum;
	const double margin =
	    kSurfaceClearance +
	    kFaceRoundoffFactor * std::numeric_limits<float>::epsilon() *
	        (std::max)({1.0, std::abs(minimum), std::abs(maximum),
	                    (width + minimumGap) * copies});
	const double distance = width + minimumGap + margin;
	if (!std::isfinite(distance) ||
	    distance > (std::numeric_limits<float>::max)()) {
		error = "The collider spacing cannot be represented.";
		return false;
	}
	float step = float(distance);
	if (double(step) < distance) {
		step = std::nextafter(step, (std::numeric_limits<float>::infinity)());
	}
	if (!std::isfinite(step) || step <= 0) {
		error = "The collider spacing cannot be represented.";
		return false;
	}
	if (!positive) {
		step = -step;
	}
	// 履歴と同じdouble加算後のfloat変換で、全組を作った際の面を確認する。
	double previousFace = positive ? maximum : minimum;
	for (int copy = 1; copy <= copies; ++copy) {
		double groupMinimum = (std::numeric_limits<double>::infinity)();
		double groupMaximum = -groupMinimum;
		Scene working;
		working.ReplaceObjects(scene.GetObjects());
		std::map<SceneObjectId, DirectX::XMFLOAT3> positions;
		for (const auto id : roots) {
			SceneWorldTransform world;
			if (!SceneHierarchy::GetWorldTransform(scene, id, world)) {
				error = "The source world transform is invalid.";
				return false;
			}

			auto position = world.GetPosition();
			assign(position,
			       float(double(component(position)) + double(step) * copy));
			positions.emplace(id, position);
		}

		if (!SceneHierarchy::SetWorldPositions(working, positions)) {
			error = "A repeated hierarchy cannot be represented at these "
			        "coordinates.";
			return false;
		}

		for (const auto id : colliderIds) {
			const auto &object = *working.FindObject(id);
			AxisAlignedBox bounds;
			if (!BoxColliderUtils::GetWorldBounds(*object.collider, working, id,
			                                      bounds)) {
				error = "A repeated collider cannot be represented at these "
				        "coordinates.";
				return false;
			}

			groupMinimum =
			    (std::min)(groupMinimum, double(component(bounds.minimum)));
			groupMaximum =
			    (std::max)(groupMaximum, double(component(bounds.maximum)));
		}
		const double gap = positive ? groupMinimum - previousFace
		                            : previousFace - groupMaximum;
		if (gap < minimumGap) {
			error = "The requested gap between repeated colliders cannot be "
			        "preserved at this coordinate precision.";
			return false;
		}
		previousFace = positive ? groupMaximum : groupMinimum;
	}
	DirectX::XMFLOAT3 result{};
	assign(result, step);
	translation = result;
	return true;
}
