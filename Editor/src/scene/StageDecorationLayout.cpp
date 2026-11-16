#include "scene/StageDecorationLayout.h"

#include <DirectXCollision.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace StageDecorationLayout {
namespace {
using DirectX::BoundingBox;
using DirectX::XMFLOAT3;

struct RouteSegment {
	XMFLOAT3 from;
	XMFLOAT3 to;
};

/// <summary>
/// 足場の上面中央を取得する
/// </summary>
XMFLOAT3 SurfaceCenter(const AxisAlignedBox &bounds) {
	return {(bounds.minimum.x + bounds.maximum.x) * 0.5f, bounds.maximum.y,
	        (bounds.minimum.z + bounds.maximum.z) * 0.5f};
}

/// <summary>
/// 装飾の配置に使う外接箱へ変換する
/// </summary>
BoundingBox GetBox(const AxisAlignedBox &bounds) {
	BoundingBox result;
	BoundingBox::CreateFromPoints(result, DirectX::XMLoadFloat3(&bounds.minimum),
	                              DirectX::XMLoadFloat3(&bounds.maximum));
	return result;
}

/// <summary>
/// 視線や移動経路の周囲の余裕を含めて、装飾との交差を調べる
/// </summary>
bool IntersectsRoute(BoundingBox bounds, const RouteSegment &segment) {
	using namespace DirectX;
	bounds.Extents.x += 0.85f;
	bounds.Extents.y += 0.55f;
	bounds.Extents.z += 0.85f;

	const XMVECTOR origin = XMLoadFloat3(&segment.from);
	const XMVECTOR delta = XMVectorSubtract(XMLoadFloat3(&segment.to), origin);
	const float length = XMVectorGetX(XMVector3Length(delta));
	float distance = 0.0f;
	return length > 0.001f &&
	       bounds.Intersects(origin, XMVectorScale(delta, 1.0f / length),
	                         distance) &&
	       distance <= length;
}

/// <summary>
/// 風に入る直前の足場を、経路が風の範囲を横切る区間から探す
/// </summary>
std::size_t FindWindApproach(std::span<const XMFLOAT3> centers,
                             const WindArea &wind) {
	const XMFLOAT3 top = SurfaceCenter(wind.bounds);
	const float lowX = wind.bounds.minimum.x;
	const float highX = wind.bounds.maximum.x;
	const float lowZ = wind.bounds.minimum.z;
	const float highZ = wind.bounds.maximum.z;
	float best = std::numeric_limits<float>::infinity();
	std::size_t approach = centers.size();

	for (std::size_t i = 0; i + 1 < centers.size(); ++i) {
		const auto &from = centers[i];
		const auto &to = centers[i + 1];
		const float x = to.x - from.x;
		const float z = to.z - from.z;
		const float lengthSquared = x * x + z * z;
		if (lengthSquared <= 0.001f) {
			continue;
		}

		const float t = std::clamp(((top.x - from.x) * x + (top.z - from.z) * z) /
		                               lengthSquared,
		                           0.0f, 1.0f);
		const float px = from.x + x * t;
		const float pz = from.z + z * t;
		const float py = from.y + (to.y - from.y) * t;
		if (px < lowX || px > highX || pz < lowZ || pz > highZ ||
		    py < wind.bounds.minimum.y - 3.5f ||
		    py > wind.bounds.maximum.y + 0.5f) {
			continue;
		}

		const float score = (px - top.x) * (px - top.x) +
		                    (pz - top.z) * (pz - top.z);
		if (score < best) {
			best = score;
			approach = i;
		}
	}

	return approach;
}
} // namespace

std::vector<Placement>
Generate(std::span<const AxisAlignedBox> route,
         std::span<const WindArea> windAreas,
         std::span<const AxisAlignedBox> occupied,
         const std::array<XMFLOAT3, 4> &footprints,
         std::uint32_t seed) {
	if (route.size() < 2) {
		return {};
	}

	std::vector<XMFLOAT3> centers;
	std::vector<RouteSegment> segments;
	std::vector<BoundingBox> reserved;
	std::vector<Placement> result;
	std::mt19937 random(seed);

	for (const auto &platform : route) {
		centers.push_back(SurfaceCenter(platform));
	}
	for (std::size_t i = 1; i < centers.size(); ++i) {
		const auto &from = centers[i - 1];
		const auto &to = centers[i];
		segments.push_back({{from.x, from.y + 1.1f, from.z},
		                    {to.x, to.y + 0.7f, to.z}});
		segments.push_back({{from.x, from.y + 2.7f, from.z},
		                    {to.x, to.y + 0.3f, to.z}});
	}

	for (const auto &item : occupied) {
		reserved.push_back(GetBox(item));
	}

	const auto place = [&](std::size_t index, Role role, float preferredYaw) {
		const auto &support = route[index];
		const auto &center = centers[index];
		const auto &previous = centers[index == 0 ? index : index - 1];
		const auto &next = centers[std::min(index + 1, centers.size() - 1)];
		const float dx = next.x - previous.x;
		const float dz = next.z - previous.z;
		const float length = std::hypot(dx, dz);
		const auto &size = footprints[static_cast<std::size_t>(role)];
		if (length < 0.001f || size.x <= 0.0f || size.y <= 0.0f || size.z <= 0.0f) {
			return;
		}

		const float radius = std::hypot(size.x, size.z) * 0.5f + 0.12f;
		const float availableX = support.maximum.x - center.x - radius;
		const float availableZ = support.maximum.z - center.z - radius;
		const bool backdrop = role == Role::BACKDROP;
		if (!backdrop && (availableX <= 0.0f || availableZ <= 0.0f)) {
			return;
		}

		const float firstSide = (random() & 1u) == 0 ? -1.0f : 1.0f;
		const float sideX = std::abs(dz) / length;
		const float sideZ = std::abs(dx) / length;
		const float lateral = backdrop ? 12.0f + radius
		    : std::min(availableX / std::max(sideX, 0.001f),
		               availableZ / std::max(sideZ, 0.001f)) * 0.92f;
		for (const float side : {firstSide, -firstSide}) {
			for (const float offset : {0.0f, -0.75f, 0.75f}) {
				const XMFLOAT3 position{
				    center.x + (dz * side * lateral + dx * offset) / length,
				    center.y - (backdrop ? 2.0f : 0.0f),
				    center.z + (-dx * side * lateral + dz * offset) / length};
				if (!backdrop && (std::abs(position.x - center.x) > availableX ||
				                  std::abs(position.z - center.z) > availableZ)) {
					continue;
				}

				const BoundingBox bounds{
				    {position.x, position.y + size.y * 0.5f, position.z},
				    {radius, size.y * 0.5f, radius}};
				if (std::ranges::any_of(reserved, [&](const BoundingBox &other) {
					    return bounds.Intersects(other);
				    }) ||
				    std::ranges::any_of(segments, [&](const RouteSegment &segment) {
					    return IntersectsRoute(bounds, segment);
				    }) ||
				    std::ranges::any_of(route, [&](const AxisAlignedBox &platform) {
					    return &platform != &support && bounds.Intersects(GetBox(platform));
				    })) {
					continue;
				}

				const float yaw = std::isfinite(preferredYaw)
				                      ? preferredYaw
				                      : std::atan2(dx, dz);
				result.push_back({role, position, yaw});
				reserved.push_back(bounds);
				return;
			}
		}
	};

	const float useRouteHeading = std::numeric_limits<float>::quiet_NaN();
	for (const auto &wind : windAreas) {
		const std::size_t approach = FindWindApproach(centers, wind);
		if (approach == centers.size()) {
			continue;
		}
		const float speed = std::hypot(wind.velocity.x, wind.velocity.z);
		const float heading = speed > 0.01f
		                          ? std::atan2(wind.velocity.x, wind.velocity.z)
		                          : useRouteHeading;
		place(approach, Role::WIND_GUIDE, heading);
		place(approach + 1, Role::LANDING_GUIDE, useRouteHeading);
	}

	for (std::size_t i = 0; i < centers.size(); ++i) {
		if (i == 0 || i + 1 == centers.size() || i % 4 == 0) {
			place(i, Role::SCENERY, useRouteHeading);
		}
		if (i % 9 == 4) {
			place(i, Role::BACKDROP, useRouteHeading);
		}
	}

	return result;
}
} // namespace StageDecorationLayout
