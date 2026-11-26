#include "physics/ColliderBounds.h"
#include "scene/SceneActivity.h"

#include "graphics/render/DebugLine.h"
#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include "physics/ColliderUtils.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneRenderer.h"
#include <cmath>
#include <numbers>

#include <array>

namespace {
/// <summary>
/// 有効なボックスを、最小・最大座標の組み合わせによる8頂点へ変換する
/// </summary>
/// <param name="box">変換するワールド座標のボックス</param>
/// <param name="corners">成功時に置き換える頂点</param>
/// <returns>true:成功 false:座標や大きさが無効</returns>
bool GetCorners(const AxisAlignedBox &box,
                std::array<DirectX::XMFLOAT3, 8> &corners) {
	if (!MathUtils::IsFinite(box.minimum) ||
	    !MathUtils::IsFinite(box.maximum) || box.minimum.x >= box.maximum.x ||
	    box.minimum.y >= box.maximum.y || box.minimum.z >= box.maximum.z) {
		return false;
	}

	std::array<DirectX::XMFLOAT3, 8> result;

	for (unsigned int i = 0; i < 8; ++i) {
		result[i] = {(i & 1) ? box.maximum.x : box.minimum.x,
		             (i & 2) ? box.maximum.y : box.minimum.y,
		             (i & 4) ? box.maximum.z : box.minimum.z};
	}

	corners = result;
	return true;
}
} // namespace

bool ColliderBounds::Update(SceneRenderer &renderer, const Scene &scene,
                            std::span<const SceneObjectId> selected,
                            Mode mode) const {
	switch (mode) {
	case Mode::NONE:
		return true;
	case Mode::SELECTED:
		for (const auto id : selected) {
			if (const auto *object = scene.FindObject(id);
			    object && !Update(renderer, scene, id)) {
				return false;
			}
		}

		return true;
	case Mode::ALL:
		for (const auto &[id, object] : scene.GetObjects()) {
			if (!SceneActivity::IsActive(scene, id)) {
				continue;
			}

			if (!Update(renderer, scene, id)) {
				return false;
			}
		}

		return true;
	default:
		return false;
	}
}

bool ColliderBounds::GetFrameBounds(const Scene &scene, SceneObjectId id,
                                    std::array<DirectX::XMFLOAT3, 8> &corners) {
	const auto *object = scene.FindObject(id);
	AxisAlignedBox bounds;
	return object && object->collider &&
	       ColliderUtils::GetWorldBounds(*object->collider, scene, id,
	                                     bounds) &&
	       GetCorners(bounds, corners);
}

bool ColliderBounds::Update(SceneRenderer &renderer, const Scene &scene,
                            SceneObjectId id) const {
	const auto *object = scene.FindObject(id);
	if (!object)
		return false;
	if (!object->collider)
		return true;
	SceneWorldTransform world;
	std::vector<DebugLine> lines;
	if (!SceneHierarchy::GetWorldTransform(scene, id, world) ||
	    !BuildLines(*object->collider, world.matrix, lines))
		return false;
	for (const auto &line : lines)
		if (!renderer.AddDebugLine(line))
			return false;
	return true;
}

bool ColliderBounds::BuildLines(const Collider &collider,
                                const DirectX::XMFLOAT4X4 &matrix,
                                std::vector<DebugLine> &lines) const {
	DirectX::XMFLOAT4X4 inverse;
	if (!collider.IsValid() ||
	    !TransformUtils::TryInverseWorldMatrix(matrix, inverse))
		return false;
	const auto world = DirectX::XMLoadFloat4x4(&matrix);
	std::vector<DebugLine> next;
	bool valid = true;
	const auto point = [&](DirectX::XMFLOAT3 local) {
		local = {local.x + collider.center.x, local.y + collider.center.y,
		         local.z + collider.center.z};
		DirectX::XMFLOAT3 result;
		DirectX::XMStoreFloat3(&result,
		                       DirectX::XMVector3TransformCoord(
		                           DirectX::XMLoadFloat3(&local), world));
		valid &= MathUtils::IsFinite(result);
		return result;
	};
	const auto line = [&](const DirectX::XMFLOAT3 &a,
	                      const DirectX::XMFLOAT3 &b) {
		next.push_back({point(a), point(b), color_});
	};

	if (collider.shape == Collider::Shape::BOX) {
		std::array<DirectX::XMFLOAT3, 8> corners;
		const auto &h = collider.halfExtents;
		for (unsigned i = 0; i < 8; ++i)
			corners[i] = {(i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y,
			              (i & 4) ? h.z : -h.z};
		for (unsigned i = 0; i < 8; ++i)
			for (unsigned axis : {1u, 2u, 4u})
				if (!(i & axis))
					line(corners[i], corners[i | axis]);
	} else {
		const float radius = collider.radius;
		const auto circle = [&](int plane, float y, float start, float end,
		                        int segments) {
			const auto at = [&](float angle) {
				const float a = std::cos(angle) * radius,
				            b = std::sin(angle) * radius;
				return plane == 0   ? DirectX::XMFLOAT3{a, b + y, 0}
				       : plane == 1 ? DirectX::XMFLOAT3{0, b + y, a}
				                    : DirectX::XMFLOAT3{a, y, b};
			};
			for (int i = 0; i < segments; ++i)
				line(at(start + (end - start) * i / segments),
				     at(start + (end - start) * (i + 1) / segments));
		};
		const float pi = std::numbers::pi_v<float>;

		if (collider.shape == Collider::Shape::SPHERE) {
			for (int plane = 0; plane < 3; ++plane)
				circle(plane, 0, 0, 2 * pi, kCircleSegments);
		} else {
			const float height = collider.halfHeight;
			for (float y : {-height, height})
				circle(2, y, 0, 2 * pi, kCircleSegments);
			for (int plane = 0; plane < 2; ++plane) {
				circle(plane, height, 0, pi, kCircleSegments / 2);
				circle(plane, -height, pi, 2 * pi, kCircleSegments / 2);
			}

			for (const DirectX::XMFLOAT3 p :
			     std::array<DirectX::XMFLOAT3, 4>{{{radius, 0, 0},
			                                       {-radius, 0, 0},
			                                       {0, 0, radius},
			                                       {0, 0, -radius}}})
				line({p.x, -height, p.z}, {p.x, height, p.z});
		}
	}

	if (!valid)
		return false;
	lines = std::move(next);
	return true;
}
