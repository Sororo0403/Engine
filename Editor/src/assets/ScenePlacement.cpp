#include "assets/ScenePlacement.h"
#include "camera/ObjectFrameBounds.h"
#include "graphics/model/ModelManager.h"
#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include "physics/ColliderUtils.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneSerializer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace {
using Corners = std::array<DirectX::XMFLOAT3, 8>;

/// <summary>
/// 配置する形状の範囲を集める。カメラなどの原点は、形状がない組でだけ使用する
/// </summary>
bool Collect(const Scene &scene, ModelManager &models,
             std::vector<Corners> &boxes) {
	for (const auto &[id, object] : scene.GetObjects()) {
		if (!TransformUtils::IsValid(object.transform)) {
			return false;
		}

		if (object.collider) {
			Corners corners;

			if (!ObjectFrameBounds::Get(scene, id, corners, true)) {
				return false;
			}

			boxes.push_back(corners);
		}

		if (object.IsRenderable()) {
			const auto model = models.Load(object.mesh->modelPath);
			Corners corners;

			if (!model || !models.GetLocalBounds(model, corners)) {
				return false;
			}

			SceneWorldTransform world;
			if (!SceneHierarchy::GetWorldTransform(scene, id, world))
				return false;
			const auto matrix = DirectX::XMLoadFloat4x4(&world.matrix);

			for (auto &point : corners) {
				DirectX::XMStoreFloat3(
				    &point, DirectX::XMVector3TransformCoord(
				                DirectX::XMLoadFloat3(&point), matrix));
			}

			boxes.push_back(corners);
		}
	}

	if (boxes.empty()) {
		for (const auto &[id, object] : scene.GetObjects()) {
			Corners corners;

			if (!ObjectFrameBounds::Get(scene, id, corners, true)) {
				return false;
			}

			boxes.push_back(corners);
		}
	}

	return !boxes.empty();
}

/// <summary>
/// 組の各軸の範囲から、中央を原点にした8頂点と中央の座標を求める
/// </summary>
bool CenteredBounds(const std::vector<Corners> &boxes, Corners &bounds,
                    DirectX::XMFLOAT3 &center) {
	std::array<double, 3> low{std::numeric_limits<double>::infinity(),
	                          std::numeric_limits<double>::infinity(),
	                          std::numeric_limits<double>::infinity()};
	std::array<double, 3> high{-low[0], -low[1], -low[2]};

	for (const auto &box : boxes) {
		for (const auto &point : box) {
			if (!MathUtils::IsFinite(point)) {
				return false;
			}

			const std::array<double, 3> value{point.x, point.y, point.z};

			for (std::size_t i = 0; i < 3; ++i) {
				low[i] = (std::min)(low[i], value[i]);
				high[i] = (std::max)(high[i], value[i]);
			}
		}
	}

	center = {static_cast<float>((low[0] + high[0]) * 0.5),
	          static_cast<float>((low[1] + high[1]) * 0.5),
	          static_cast<float>((low[2] + high[2]) * 0.5)};
	for (std::size_t i = 0; i < bounds.size(); ++i) {
		bounds[i] = {
		    static_cast<float>(((i & 1) ? high[0] : low[0]) - center.x),
		    static_cast<float>(((i & 2) ? high[1] : low[1]) - center.y),
		    static_cast<float>(((i & 4) ? high[2] : low[2]) - center.z)};
	}

	return MathUtils::IsFinite(center);
}
} // namespace

bool ScenePlacement::Place(Scene &objects, const Scene &target, const Ray &ray,
                           ModelManager &models,
                           std::optional<DirectX::XMFLOAT3> grid,
                           float planeHeight) {
	ModelPlacement::Surface surface;

	if (!ModelPlacement::GetSurface(ray, target, surface, planeHeight)) {
		return false;
	}

	std::vector<Corners> boxes;

	if (!Collect(objects, models, boxes)) {
		return false;
	}

	Corners bounds;
	DirectX::XMFLOAT3 center, placedCenter;

	if (!CenteredBounds(boxes, bounds, center) ||
	    !ModelPlacement::GetSurfaceTranslation(surface, bounds, placedCenter)) {
		return false;
	}

	if (grid) {
		DirectX::XMFLOAT3 snapped;

		if (!MathUtils::SnapToGrid(placedCenter, *grid, snapped)) {
			return false;
		}

		if (surface.onSurface) {
			if (surface.normal.x != 0) {
				snapped.x = placedCenter.x;
			} else if (surface.normal.y != 0) {
				snapped.y = placedCenter.y;
			} else {
				snapped.z = placedCenter.z;
			}
		}

		placedCenter = snapped;
	}

	const DirectX::XMFLOAT3 delta{placedCenter.x - center.x,
	                              placedCenter.y - center.y,
	                              placedCenter.z - center.z};
	if (!MathUtils::IsFinite(delta)) {
		return false;
	}

	return objects.ApplyTransaction([&](Scene &working) {
		for (const auto &[id, data] : working.GetObjects()) {
			auto *object = working.FindObject(id);

			if (object->GetParentId() == 0) {
				auto &position = object->transform.translation;
				position = {position.x + delta.x, position.y + delta.y,
				            position.z + delta.z};
			}

			if (object->respawn) {
				auto &respawn = *object->respawn;
				respawn.threshold += respawn.Coordinate(delta);
				respawn.position = {respawn.position.x + delta.x,
				                    respawn.position.y + delta.y,
				                    respawn.position.z + delta.z};
			}
		}

		// 最終のfloat加算後も、全形状が面の内側へ入り込んでいないことを確認する。
		std::vector<Corners> placedBoxes;

		if (!Collect(working, models, placedBoxes)) {
			return false;
		}

		if (surface.onSurface) {
			const std::array<float, 3> normal{
			    surface.normal.x, surface.normal.y, surface.normal.z};
			const std::array<float, 3> at{
			    surface.position.x, surface.position.y, surface.position.z};
			std::size_t axis = 0;

			while (axis < normal.size() && normal[axis] == 0) {
				++axis;
			}

			if (axis == normal.size()) {
				return false;
			}

			double gap = std::numeric_limits<double>::infinity();
			double span = 0;

			for (const auto &box : placedBoxes) {
				for (const auto &point : box) {
					if (!MathUtils::IsFinite(point)) {
						return false;
					}

					const std::array<float, 3> value{point.x, point.y, point.z};
					const double distance =
					    (double(value[axis]) - at[axis]) * normal[axis];
					gap = (std::min)(gap, distance);
					span = (std::max)(span, std::abs(distance));
				}
			}

			const std::array<float, 3> originalCenter{center.x, center.y,
			                                          center.z};
			const std::array<float, 3> movement{delta.x, delta.y, delta.z};
			const double tolerance =
			    kSurfaceClearance +
			    kRoundoffFactor * std::numeric_limits<float>::epsilon() *
			        (std::max)({1.0, std::abs(double(at[axis])), span,
			                    std::abs(double(originalCenter[axis])),
			                    std::abs(double(movement[axis]))});
			if (gap < 0 || gap > tolerance) {
				return false;
			}
		}

		std::string text;
		return SceneSerializer::Serialize(working, text);
	});
}
