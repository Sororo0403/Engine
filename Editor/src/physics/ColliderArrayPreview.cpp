#include "physics/ColliderArrayPreview.h"

#include "math/TransformUtils.h"
#include "physics/BoxColliderUtils.h"
#include "physics/ColliderBounds.h"
#include "scene/Scene.h"
#include "scene/SceneRenderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <set>

bool ColliderArrayPreview::BuildBounds(
    const Scene &scene, std::span<const SceneObjectId> ids,
    const ObjectArraySettings &settings,
    std::vector<AxisAlignedBox> &bounds) const {
	if (!settings.IsValid(ids.size())) {
		return false;
	}

	std::set<SceneObjectId> seen;

	for (const auto id : ids) {
		const auto *object = scene.FindObject(id);

		if (!seen.insert(id).second || !object ||
		    !TransformUtils::IsValid(object->transform)) {
			return false;
		}
	}

	std::vector<AxisAlignedBox> result;
	result.reserve(settings.copies);

	for (int copy = 1; copy <= settings.copies; ++copy) {
		std::optional<AxisAlignedBox> combined;

		for (const auto id : ids) {
			const auto &object = *scene.FindObject(id);
			auto transform = object.transform;
			const auto &position = transform.translation;
			const std::array<double, 3> translated{
			    double(position.x) + double(settings.offset.x) * copy,
			    double(position.y) + double(settings.offset.y) * copy,
			    double(position.z) + double(settings.offset.z) * copy};

			if (std::any_of(translated.begin(), translated.end(),
			                [](double value) {
				                return !std::isfinite(value) ||
				                       std::abs(value) >
				                           (std::numeric_limits<float>::max)();
			                })) {
				return false;
			}

			transform.translation = {float(translated[0]), float(translated[1]),
			                         float(translated[2])};

			if (!object.collider) {
				continue;
			}

			AxisAlignedBox box;

			if (!BoxColliderUtils::GetWorldBounds(*object.collider, transform,
			                                      box)) {
				return false;
			}

			if (!combined) {
				combined = box;
				continue;
			}

			combined->minimum = {
			    (std::min)(combined->minimum.x, box.minimum.x),
			    (std::min)(combined->minimum.y, box.minimum.y),
			    (std::min)(combined->minimum.z, box.minimum.z)};
			combined->maximum = {
			    (std::max)(combined->maximum.x, box.maximum.x),
			    (std::max)(combined->maximum.y, box.maximum.y),
			    (std::max)(combined->maximum.z, box.maximum.z)};
		}

		if (combined) {
			result.push_back(*combined);
		}
	}

	bounds.swap(result);
	return true;
}

bool ColliderArrayPreview::Update(SceneRenderer &renderer, const Scene &scene,
                                  std::span<const SceneObjectId> ids,
                                  const ObjectArraySettings &settings) const {
	std::vector<AxisAlignedBox> bounds;

	if (!BuildBounds(scene, ids, settings, bounds)) {
		return true;
	}

	ColliderBounds outline;

	for (const auto &box : bounds) {
		ColliderBounds::Lines lines;

		if (!outline.BuildLines(box, lines)) {
			return false;
		}

		for (auto &line : lines) {
			line.color = color_;

			if (!renderer.AddDebugLine(line)) {
				return false;
			}
		}
	}

	return true;
}
