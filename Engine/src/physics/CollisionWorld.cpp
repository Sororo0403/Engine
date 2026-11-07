#include "physics/CollisionWorld.h"

#include "math/MathUtils.h"
#include "physics/ColliderUtils.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
/// <summary>
/// 有限で正の体積を持つ問い合わせ範囲か確認する
/// </summary>
bool Valid(const AxisAlignedBox &box) {
	return MathUtils::IsFinite(box.minimum) &&
	       MathUtils::IsFinite(box.maximum) && box.minimum.x < box.maximum.x &&
	       box.minimum.y < box.maximum.y && box.minimum.z < box.maximum.z;
}

/// <summary>
/// 問い合わせ用のAABBを、実形状の共通判定へ渡す箱へ変換する
/// </summary>
bool QueryShape(const AxisAlignedBox &box, WorldCollider &shape) {
	if (!Valid(box))
		return false;
	const auto middle = [](float a, float b) {
		return static_cast<float>((double(a) + b) * .5);
	};
	const auto half = [](float a, float b) {
		return static_cast<float>((double(b) - a) * .5);
	};
	Collider c{{middle(box.minimum.x, box.maximum.x),
	            middle(box.minimum.y, box.maximum.y),
	            middle(box.minimum.z, box.maximum.z)},
	           {half(box.minimum.x, box.maximum.x),
	            half(box.minimum.y, box.maximum.y),
	            half(box.minimum.z, box.maximum.z)}};
	DirectX::XMFLOAT4X4 identity;
	DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
	std::string error;
	return WorldCollider::Create(c, identity, shape, error);
}

using Vector = std::array<double, 3>;

double Dot(const Vector &a, const Vector &b) {
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vector Cross(const Vector &a, const Vector &b) {
	return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
	        a[0] * b[1] - a[1] * b[0]};
}

/// <summary>
/// AABBと合成した箱の15分離軸で移動区間を調べ、接触開始時も面の正しい法線を返す
/// </summary>
/// <param name="body">問い合わせ元のAABB</param>
/// <param name="box">障害物のローカル箱</param>
/// <param name="matrix">回転・反転・せん断を含められる合成行列</param>
/// <param name="movement">ワールド座標の直線移動量</param>
/// <param name="tolerance">開始時に許容する面の丸め誤差</param>
/// <returns>体積を共有し始める最初の位置。面を滑る・離れるだけの場合はnullopt</returns>
std::optional<ColliderCastHit> SweepBox(const AxisAlignedBox &body,
                                        const Collider &box,
                                        const DirectX::XMFLOAT4X4 &matrix,
                                        const DirectX::XMFLOAT3 &movement,
                                        double tolerance) {
	const Vector center{(double(body.minimum.x) + body.maximum.x) * .5,
	                    (double(body.minimum.y) + body.maximum.y) * .5,
	                    (double(body.minimum.z) + body.maximum.z) * .5};
	const Vector half{(double(body.maximum.x) - body.minimum.x) * .5,
	                  (double(body.maximum.y) - body.minimum.y) * .5,
	                  (double(body.maximum.z) - body.minimum.z) * .5};
	const Vector boxCenter{
	    double(box.center.x) * matrix._11 + double(box.center.y) * matrix._21 +
	        double(box.center.z) * matrix._31 + matrix._41,
	    double(box.center.x) * matrix._12 + double(box.center.y) * matrix._22 +
	        double(box.center.z) * matrix._32 + matrix._42,
	    double(box.center.x) * matrix._13 + double(box.center.y) * matrix._23 +
	        double(box.center.z) * matrix._33 + matrix._43};
	const std::array<Vector, 3> edges{
	    {{double(box.halfExtents.x) * matrix._11,
	      double(box.halfExtents.x) * matrix._12,
	      double(box.halfExtents.x) * matrix._13},
	     {double(box.halfExtents.y) * matrix._21,
	      double(box.halfExtents.y) * matrix._22,
	      double(box.halfExtents.y) * matrix._23},
	     {double(box.halfExtents.z) * matrix._31,
	      double(box.halfExtents.z) * matrix._32,
	      double(box.halfExtents.z) * matrix._33}}};
	const std::array<Vector, 3> world{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
	std::array<Vector, 15> axes;
	for (int i = 0; i < 3; ++i) {
		axes[i] = world[i];
		axes[i + 3] = Cross(edges[(i + 1) % 3], edges[(i + 2) % 3]);
	}
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			axes[6 + i * 3 + j] = Cross(world[i], edges[j]);
	const Vector delta{center[0] - boxCenter[0], center[1] - boxCenter[1],
	                   center[2] - boxCenter[2]};
	const Vector velocity{movement.x, movement.y, movement.z};
	double enter = -std::numeric_limits<double>::infinity(),
	       exit = std::numeric_limits<double>::infinity();
	double minimumDepth = std::numeric_limits<double>::infinity();
	Vector firstNormal{}, closestNormal{};
	for (auto axis : axes) {
		const double length = std::hypot(axis[0], axis[1], axis[2]);
		if (length <= std::numeric_limits<double>::epsilon())
			continue;
		for (auto &v : axis)
			v /= length;
		const double radius =
		    half[0] * std::abs(axis[0]) + half[1] * std::abs(axis[1]) +
		    half[2] * std::abs(axis[2]) + std::abs(Dot(axis, edges[0])) +
		    std::abs(Dot(axis, edges[1])) + std::abs(Dot(axis, edges[2]));
		const double offset = Dot(delta, axis), speed = Dot(velocity, axis),
		             depth = radius - std::abs(offset);
		const double sign = offset >= 0 ? 1. : -1.;
		if (depth < minimumDepth) {
			minimumDepth = depth;
			closestNormal = {axis[0] * sign, axis[1] * sign, axis[2] * sign};
		}
		if (std::abs(speed) <= 1e-12) {
			if (depth <= tolerance)
				return std::nullopt;
			continue;
		}
		double first = (-radius - offset) / speed,
		       last = (radius - offset) / speed;
		if (first > last)
			std::swap(first, last);
		if (first > enter) {
			enter = first;
			const double outward = speed > 0 ? -1. : 1.;
			firstNormal = {axis[0] * outward, axis[1] * outward,
			               axis[2] * outward};
		}
		exit = (std::min)(exit, last);
		if (enter >= exit)
			return std::nullopt;
	}
	if (exit < 0 || enter > 1)
		return std::nullopt;
	Vector normal = firstNormal;
	if (enter <= 0) {
		if (minimumDepth <= tolerance &&
		    Dot(closestNormal, velocity) >= -.0000001)
			return std::nullopt;
		normal = closestNormal;
		enter = 0;
	}
	return ColliderCastHit{enter,
	                       {static_cast<float>(normal[0]),
	                        static_cast<float>(normal[1]),
	                        static_cast<float>(normal[2])}};
}
} // namespace

bool CollisionWorld::Rebuild(const Scene &scene, SceneObjectId ignoredObject,
                             SceneObjectId *invalidObject,
                             const RigidBodyWorld *rigidBodies) {
	return Rebuild(scene, std::span<const SceneObjectId>{&ignoredObject, 1},
	               invalidObject, rigidBodies);
}

bool CollisionWorld::Rebuild(const Scene &scene,
                             std::span<const SceneObjectId> ignoredObjects,
                             SceneObjectId *invalidObject,
                             const RigidBodyWorld *rigidBodies) {
	if (invalidObject) {
		*invalidObject = 0;
	}

	SceneHierarchy::WorldTransforms transforms;
	if (!SceneHierarchy::BuildWorldTransforms(scene, transforms)) {
		return false;
	}
	SceneActivity::States active;
	if (!SceneActivity::BuildStates(scene, active)) {
		return false;
	}
	std::vector<Obstacle> candidate;

	for (const auto &[id, object] : scene.GetObjects()) {
		if (!active.at(id) || !object.collider ||
		    std::find(ignoredObjects.begin(), ignoredObjects.end(), id) !=
		        ignoredObjects.end()) {
			continue;
		}

		AxisAlignedBox bounds;

		if (!ColliderUtils::GetWorldBounds(*object.collider,
		                                   transforms.at(id).matrix, bounds)) {
			if (invalidObject) {
				*invalidObject = id;
			}

			return false;
		}

		if (!object.collider->isTrigger) {
			WorldCollider geometry;
			std::string error;
			if (!WorldCollider::Create(*object.collider,
			                           transforms.at(id).matrix, geometry,
			                           error)) {
				if (invalidObject)
					*invalidObject = id;
				return false;
			}
			candidate.push_back({id, bounds, object.collider->filter,
			                     std::move(geometry), *object.collider,
			                     transforms.at(id).matrix});
		}
	}

	std::vector<SceneObjectId> ignored(ignoredObjects.begin(),
	                                   ignoredObjects.end());
	std::optional<RigidBodyWorld> physics;
	if (rigidBodies)
		physics = *rigidBodies;
	rigidBodies_.swap(physics);
	ignoredBodies_.swap(ignored);
	obstacles_.swap(candidate);
	return true;
}

bool CollisionWorld::CanPlace(
    const AxisAlignedBox &body,
    const std::optional<CollisionFilter> &filter) const {
	if (!Valid(body) || (filter && !filter->IsValid())) {
		return false;
	}

	if (rigidBodies_)
		return rigidBodies_->CanPlace(body, ignoredBodies_, filter);

	WorldCollider query;
	if (!QueryShape(body, query))
		return false;
	for (const auto &obstacle : obstacles_) {
		if (filter && !filter->CanCollide(obstacle.filter))
			continue;
		bool overlaps = false;
		if (!query.Overlaps(obstacle.geometry, kOverlapTolerance, overlaps) ||
		    overlaps)
			return false;
	}

	return true;
}

bool CollisionWorld::FindOverlaps(const Scene &scene,
                                  const AxisAlignedBox &body,
                                  std::vector<SceneObjectId> &objects,
                                  SceneObjectId ignoredObject,
                                  const std::optional<CollisionFilter> &filter,
                                  bool includeTriggers) {
	if (!Valid(body) || (filter && !filter->IsValid())) {
		return false;
	}

	SceneHierarchy::WorldTransforms transforms;
	if (!SceneHierarchy::BuildWorldTransforms(scene, transforms)) {
		return false;
	}
	SceneActivity::States active;
	if (!SceneActivity::BuildStates(scene, active)) {
		return false;
	}
	std::vector<SceneObjectId> result;
	WorldCollider query;
	if (!QueryShape(body, query))
		return false;

	for (const auto &[id, object] : scene.GetObjects()) {
		if (!active.at(id) || id == ignoredObject || !object.collider) {
			continue;
		}

		AxisAlignedBox box;

		if (!ColliderUtils::GetWorldBounds(*object.collider,
		                                   transforms.at(id).matrix, box)) {
			return false;
		}

		if ((includeTriggers || !object.collider->isTrigger) &&
		    (!filter || filter->CanCollide(object.collider->filter))) {
			WorldCollider geometry;
			std::string error;
			bool overlap = false;
			if (!WorldCollider::Create(*object.collider,
			                           transforms.at(id).matrix, geometry,
			                           error) ||
			    !query.Overlaps(geometry, kOverlapTolerance, overlap))
				return false;
			if (overlap)
				result.push_back(id);
		}
	}

	objects.swap(result);
	return true;
}

bool CollisionWorld::FindColliderOverlaps(const Scene &scene,
                                          SceneObjectId objectId,
                                          std::vector<SceneObjectId> &objects) {
	const auto *source = scene.FindObject(objectId);
	if (!source || !source->collider)
		return false;
	SceneHierarchy::WorldTransforms transforms;
	SceneActivity::States active;
	if (!SceneHierarchy::BuildWorldTransforms(scene, transforms) ||
	    !SceneActivity::BuildStates(scene, active))
		return false;
	WorldCollider query;
	std::string error;
	if (!WorldCollider::Create(*source->collider,
	                           transforms.at(objectId).matrix, query, error))
		return false;
	std::vector<SceneObjectId> next;
	for (const auto &[id, object] : scene.GetObjects()) {
		if (id == objectId || !object.collider || !active.at(id))
			continue;
		WorldCollider shape;
		bool overlap = false;
		if (!WorldCollider::Create(*object.collider, transforms.at(id).matrix,
		                           shape, error) ||
		    !query.Overlaps(shape, kOverlapTolerance, overlap))
			return false;
		if (overlap)
			next.push_back(id);
	}
	objects = std::move(next);
	return true;
}

bool CollisionWorld::Sweep(const AxisAlignedBox &body,
                           const DirectX::XMFLOAT3 &displacement,
                           CollisionSweepResult &result,
                           const std::optional<CollisionFilter> &filter) const {
	if (!Valid(body) || !MathUtils::IsFinite(displacement) ||
	    (filter && !filter->IsValid())) {
		return false;
	}

	if (rigidBodies_)
		return rigidBodies_->Sweep(body, displacement, result, ignoredBodies_,
		                           filter);

	WorldCollider query;
	if (!QueryShape(body, query))
		return false;
	CollisionSweepResult candidate;
	for (const auto &obstacle : obstacles_) {
		if (filter && !filter->CanCollide(obstacle.filter))
			continue;
		std::optional<ColliderCastHit> hit;
		if (obstacle.collider.shape == Collider::Shape::BOX) {
			hit = SweepBox(body, obstacle.collider, obstacle.matrix,
			               displacement, kOverlapTolerance);
			if (hit &&
			    (!candidate.objectId || hit->fraction < candidate.fraction ||
			     (hit->fraction == candidate.fraction &&
			      obstacle.objectId < candidate.objectId)))
				candidate = {obstacle.objectId, hit->fraction, hit->normal};
			continue;
		}
		if (!query.Cast(obstacle.geometry, displacement, hit))
			return false;
		if (!hit)
			continue;
		bool volume = false;
		if (!query.Crosses(obstacle.geometry, displacement, kOverlapTolerance,
		                   volume))
			return false;
		const double into = double(hit->normal.x) * displacement.x +
		                    double(hit->normal.y) * displacement.y +
		                    double(hit->normal.z) * displacement.z;
		if (!volume && !(hit->fraction >= .999999 && into < -.0000001))
			continue;
		if (!candidate.objectId || hit->fraction < candidate.fraction ||
		    (hit->fraction == candidate.fraction &&
		     obstacle.objectId < candidate.objectId))
			candidate = {obstacle.objectId, hit->fraction, hit->normal};
	}

	result = candidate;
	return true;
}

bool CollisionWorld::Move(const AxisAlignedBox &body,
                          const DirectX::XMFLOAT3 &displacement,
                          CollisionMoveResult &result,
                          const std::optional<CollisionFilter> &filter) const {
	if (!CanPlace(body, filter) || !MathUtils::IsFinite(displacement))
		return false;
	return MoveAlongSurfaces(body, displacement, result, filter);
}

bool CollisionWorld::MoveAlongSurfaces(
    const AxisAlignedBox &body, const DirectX::XMFLOAT3 &displacement,
    CollisionMoveResult &result,
    const std::optional<CollisionFilter> &filter) const {
	DirectX::XMFLOAT3 moved{}, remaining = displacement;
	CollisionMoveResult next;
	for (int iteration = 0; iteration < kMaximumSlideIterations; ++iteration) {
		auto box = body;
		box.minimum = {body.minimum.x + moved.x, body.minimum.y + moved.y,
		               body.minimum.z + moved.z};
		box.maximum = {body.maximum.x + moved.x, body.maximum.y + moved.y,
		               body.maximum.z + moved.z};
		CollisionSweepResult hit;
		if (!Sweep(box, remaining, hit, filter))
			return false;
		const double length = std::sqrt(double(remaining.x) * remaining.x +
		                                double(remaining.y) * remaining.y +
		                                double(remaining.z) * remaining.z);
		if (length < 0.0000001)
			break;
		const float fraction =
		    hit.objectId
		        ? static_cast<float>(
		              (std::max)(0.0, hit.fraction - kSkinWidth / length))
		        : 1;
		moved.x += remaining.x * fraction;
		moved.y += remaining.y * fraction;
		moved.z += remaining.z * fraction;
		if (!hit.objectId)
			break;
		next.grounded |= hit.normal.y > .5f && remaining.y < 0;
		next.hitCeiling |= hit.normal.y < -.5f && remaining.y > 0;
		next.hitWall |= std::abs(hit.normal.y) <= .5f;
		remaining.x *= 1 - fraction;
		remaining.y *= 1 - fraction;
		remaining.z *= 1 - fraction;
		const float into = remaining.x * hit.normal.x +
		                   remaining.y * hit.normal.y +
		                   remaining.z * hit.normal.z;
		if (into < 0) {
			remaining.x -= hit.normal.x * into;
			remaining.y -= hit.normal.y * into;
			remaining.z -= hit.normal.z * into;
		} else
			break;
	}
	auto finalBox = body;
	finalBox.minimum = {body.minimum.x + moved.x, body.minimum.y + moved.y,
	                    body.minimum.z + moved.z};
	finalBox.maximum = {body.maximum.x + moved.x, body.maximum.y + moved.y,
	                    body.maximum.z + moved.z};
	if (!CanPlace(finalBox, filter))
		return false;
	next.displacement = moved;
	result = next;
	return true;
}
