#include "selection/SelectionMeasurement.h"

#include "math/MathUtils.h"
#include "physics/ColliderUtils.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneRenderer.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

bool SelectionMeasurement::Measure(const Scene &scene,
                                   std::span<const SceneObjectId> ids,
                                   Result &result, std::string &error) {
	Result candidate;
	candidate.objects = ids.size();
	std::set<SceneObjectId> seen;
	std::array<std::optional<AxisAlignedBox>, 2> pairBounds;
	Pair pair;

	for (std::size_t i = 0; i < ids.size(); ++i) {
		const auto *object = scene.FindObject(ids[i]);
		SceneWorldTransform world;

		if (!object || !seen.insert(ids[i]).second ||
		    !SceneHierarchy::GetWorldTransform(scene, ids[i], world)) {
			error = "Select existing objects with finite positions and no "
			        "duplicate IDs.";
			return false;
		}

		if (ids.size() == 2) {
			if (i == 0) {
				pair.from = ids[i];
				pair.start = world.GetPosition();
			} else {
				pair.to = ids[i];
				pair.end = world.GetPosition();
			}
		}

		if (!object->collider) {
			continue;
		}

		AxisAlignedBox bounds;

		if (!ColliderUtils::GetWorldBounds(*object->collider, world.matrix,
		                                   bounds)) {
			++candidate.invalidColliders;
			continue;
		}
		++candidate.colliders;

		if (ids.size() == 2) {
			pairBounds[i] = bounds;
		}

		if (!candidate.colliderBounds) {
			candidate.colliderBounds = bounds;
		} else {
			auto &combined = *candidate.colliderBounds;
			combined.minimum = {
			    (std::min)(combined.minimum.x, bounds.minimum.x),
			    (std::min)(combined.minimum.y, bounds.minimum.y),
			    (std::min)(combined.minimum.z, bounds.minimum.z)};
			combined.maximum = {
			    (std::max)(combined.maximum.x, bounds.maximum.x),
			    (std::max)(combined.maximum.y, bounds.maximum.y),
			    (std::max)(combined.maximum.z, bounds.maximum.z)};
		}
	}

	if (ids.size() == 2) {
		pair.delta = {double(pair.end.x) - pair.start.x,
		              double(pair.end.y) - pair.start.y,
		              double(pair.end.z) - pair.start.z};
		pair.distance = std::hypot(pair.delta[0], pair.delta[1], pair.delta[2]);
		pair.horizontalDistance = std::hypot(pair.delta[0], pair.delta[2]);

		if (pairBounds[0] && pairBounds[1]) {
			const auto &a = *pairBounds[0];
			const auto &b = *pairBounds[1];
			const auto gap = [](float amin, float amax, float bmin,
			                    float bmax) {
				return (
				    std::max)({0.0, double(bmin) - amax, double(amin) - bmax});
			};
			pair.colliderGap = {
			    {gap(a.minimum.x, a.maximum.x, b.minimum.x, b.maximum.x),
			     gap(a.minimum.y, a.maximum.y, b.minimum.y, b.maximum.y),
			     gap(a.minimum.z, a.maximum.z, b.minimum.z, b.maximum.z)}};
			const auto &g = *pair.colliderGap;
			pair.colliderDistance = std::hypot(g[0], g[1], g[2]);
			pair.collidersOverlap =
			    a.minimum.x < b.maximum.x && a.maximum.x > b.minimum.x &&
			    a.minimum.y < b.maximum.y && a.maximum.y > b.minimum.y &&
			    a.minimum.z < b.maximum.z && a.maximum.z > b.minimum.z;
		}

		candidate.pair = pair;
	}

	result = std::move(candidate);
	error.clear();
	return true;
}

bool SelectionMeasurement::BuildLine(const Result &result, DebugLine &line) {
	if (!result.pair || result.pair->distance <= 0 ||
	    !MathUtils::IsFinite(result.pair->start) ||
	    !MathUtils::IsFinite(result.pair->end)) {
		return false;
	}
	line = {result.pair->start, result.pair->end, {1.0f, 0.75f, 0.1f, 1.0f}};
	return true;
}

bool SelectionMeasurement::AppendLine(SceneRenderer &renderer,
                                      const Scene &scene,
                                      std::span<const SceneObjectId> ids) {
	if (ids.size() != 2) {
		return true;
	}

	Result result;
	std::string error;
	DebugLine line;
	return !Measure(scene, ids, result, error) || !BuildLine(result, line) ||
	       renderer.AddDebugLine(line);
}
