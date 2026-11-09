#include "physics/ColliderArrayPreview.h"

#include "math/TransformUtils.h"
#include "physics/ColliderBounds.h"
#include "physics/ColliderUtils.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
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
	std::vector<SceneObjectId> roots, expanded;
	if (!SceneHierarchy::GetSelectionRoots(scene, ids, roots) ||
	    !SceneHierarchy::ExpandSelection(scene, ids, expanded) ||
	    !settings.IsValid(expanded.size())) {
		return false;
	}

	SceneHierarchy::WorldTransforms original;

	if (!SceneHierarchy::BuildWorldTransforms(scene, original)) {
		return false;
	}

	std::vector<AxisAlignedBox> result;
	const auto count = settings.GetCopyCount();
	result.reserve(count);

	for (int copy = 1; copy <= count; ++copy) {
		std::optional<AxisAlignedBox> combined;

		Scene working;
		working.ReplaceObjects(scene.GetObjects());
		std::map<SceneObjectId, DirectX::XMFLOAT3> positions;

		for (const auto id : roots) {
			const auto position = original.at(id).GetPosition();
			positions.emplace(id, settings.GetPosition(position, copy));
		}

		if (!SceneHierarchy::SetWorldPositions(working, positions)) {
			return false;
		}

		for (const auto id : expanded) {
			const auto &object = *working.FindObject(id);

			if (!object.collider) {
				continue;
			}

			AxisAlignedBox box;

			if (!ColliderUtils::GetWorldBounds(*object.collider, working, id,
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
