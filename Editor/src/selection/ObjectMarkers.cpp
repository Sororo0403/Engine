#include "selection/ObjectMarkers.h"
#include "scene/SceneActivity.h"

#include "math/MathUtils.h"
#include "math/Ray.h"
#include "math/TransformUtils.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneRenderer.h"
#include <DirectXCollision.h>
#include <algorithm>
#include <cmath>

bool ObjectMarkers::GetBounds(const SceneObject &object, Corners &corners,
                              bool missingModel) {
	if (object.parentId != 0 ||
	    (object.HasMesh() && object.meshRenderer && !missingModel) ||
	    !TransformUtils::IsValid(object.transform)) {
		return false;
	}

	Corners result;
	const auto &position = object.transform.translation;

	for (std::size_t i = 0; i < result.size(); ++i) {
		result[i] = {position.x + ((i & 1) ? kHalfExtent : -kHalfExtent),
		             position.y + ((i & 2) ? kHalfExtent : -kHalfExtent),
		             position.z + ((i & 4) ? kHalfExtent : -kHalfExtent)};

		if (!MathUtils::IsFinite(result[i])) {
			return false;
		}
	}

	if (result[0].x >= result[7].x || result[0].y >= result[7].y ||
	    result[0].z >= result[7].z) {
		return false;
	}

	corners = result;
	return true;
}

bool ObjectMarkers::GetBounds(const Scene &scene, SceneObjectId id,
                              Corners &corners, bool missingModel) {
	const auto *object = scene.FindObject(id);
	SceneWorldTransform world;
	if (!object || !SceneHierarchy::GetWorldTransform(scene, id, world))
		return false;
	// 目印は拡縮に依存しない固定サイズなので、合成後の原点だけを使用する。
	auto marker = *object;
	marker.parentId = 0;
	marker.transform.translation = world.GetPosition();
	return GetBounds(marker, corners, missingModel);
}

bool ObjectMarkers::BuildLines(const SceneObject &object, bool selected,
                               Lines &lines, bool missingModel) {
	Corners corners;

	if (!GetBounds(object, corners, missingModel)) {
		return false;
	}

	const DirectX::XMFLOAT4 color =
	    selected       ? DirectX::XMFLOAT4{1.0f, 0.85f, 0.15f, 1.0f}
	    : missingModel ? DirectX::XMFLOAT4{1.0f, 0.2f, 0.2f, 1.0f}
	    : (object.pointLight || object.spotLight)
	        ? DirectX::XMFLOAT4{1.0f, 0.75f, 0.2f, 1.0f}
	    : object.camera   ? DirectX::XMFLOAT4{0.2f, 0.85f, 1.0f, 1.0f}
	    : object.collider ? DirectX::XMFLOAT4{0.2f, 1.0f, 0.3f, 1.0f}
	                      : DirectX::XMFLOAT4{0.85f, 0.85f, 0.85f, 1.0f};
	Lines result;
	std::size_t count = 0;

	for (std::size_t i = 0; i < corners.size(); ++i) {
		for (std::size_t axis = 1; axis <= 4; axis <<= 1) {
			if ((i & axis) == 0) {
				result[count++] = {corners[i], corners[i | axis], color};
			}
		}
	}

	lines = result;
	return true;
}

SceneObjectId ObjectMarkers::Pick(const Scene &scene, const Ray &ray,
                                  float modelDistance,
                                  const SceneRenderer *renderer,
                                  std::span<const SceneObjectId> excluded) {
	const double length = std::hypot(static_cast<double>(ray.direction.x),
	                                 static_cast<double>(ray.direction.y),
	                                 static_cast<double>(ray.direction.z));

	if (!MathUtils::IsFinite(ray.origin) ||
	    !MathUtils::IsFinite(ray.direction) ||
	    !std::isfinite(ray.maxDistance) || ray.maxDistance <= 0 ||
	    !std::isfinite(modelDistance) || modelDistance < 0 ||
	    !std::isfinite(length) ||
	    std::abs(length - 1.0) > kDirectionTolerance) {
		return 0;
	}

	const auto direction =
	    DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&ray.direction));
	float nearest = (std::min)(ray.maxDistance, modelDistance);
	SceneObjectId picked = 0;

	for (const auto &[id, object] : scene.GetObjects()) {
		if (!SceneActivity::IsActive(scene, id) ||
		    std::find(excluded.begin(), excluded.end(), id) != excluded.end()) {
			continue;
		}

		Corners corners;

		if (!GetBounds(scene, id, corners,
		               renderer && renderer->IsModelUnavailable(id))) {
			continue;
		}

		DirectX::BoundingBox box;
		DirectX::BoundingBox::CreateFromPoints(
		    box, corners.size(), corners.data(), sizeof(corners[0]));
		float distance = 0;

		if (box.Intersects(DirectX::XMLoadFloat3(&ray.origin), direction,
		                   distance) &&
		    std::isfinite(distance)) {
			// レイ始点が目印の内側にある場合は、始点で交差したものとして扱う。
			distance = (std::max)(0.0f, distance);

			if (distance < nearest) {
				nearest = distance;
				picked = id;
			}
		}
	}

	return picked;
}

bool ObjectMarkers::Update(SceneRenderer &renderer, const Scene &scene,
                           std::span<const SceneObjectId> selected) const {
	for (const auto &[id, object] : scene.GetObjects()) {
		if (!SceneActivity::IsActive(scene, id) &&
		    std::find(selected.begin(), selected.end(), id) == selected.end()) {
			continue;
		}
		Lines lines;

		SceneWorldTransform world;
		if (!SceneHierarchy::GetWorldTransform(scene, id, world))
			continue;
		auto marker = object;
		marker.parentId = 0;
		marker.transform.translation = world.GetPosition();
		if (!BuildLines(marker,
		                std::find(selected.begin(), selected.end(), id) !=
		                    selected.end(),
		                lines, renderer.IsModelUnavailable(id))) {
			continue;
		}

		for (const auto &line : lines) {
			if (!renderer.AddDebugLine(line)) {
				return false;
			}
		}
	}

	return true;
}
