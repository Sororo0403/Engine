#include "gameplay/ScenePhysics.h"
#include "math/TransformUtils.h"
#include "physics/BoxColliderUtils.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <stdexcept>
#include <vector>

namespace {
/// <summary>
/// 剛体の更新に影響する設定が同じか確認する。姿勢は別に同期する
/// </summary>
bool SameDefinition(const RigidBodyDescription &a,
                    const RigidBodyDescription &b) {
	return a.object == b.object && a.collider == b.collider &&
	       a.rigidBody == b.rigidBody &&
	       a.transform.scale.x == b.transform.scale.x &&
	       a.transform.scale.y == b.transform.scale.y &&
	       a.transform.scale.z == b.transform.scale.z;
}

/// <summary>
/// 親子変換の往復による微小誤差だけで、静止中の剛体を起こさない
/// </summary>
bool SamePose(const Transform &a, const Transform &b) {
	const auto &x = a.translation, &y = b.translation;
	const double dot = double(a.rotation.x) * b.rotation.x +
	                   double(a.rotation.y) * b.rotation.y +
	                   double(a.rotation.z) * b.rotation.z +
	                   double(a.rotation.w) * b.rotation.w;
	return std::abs(x.x - y.x) < .00001f && std::abs(x.y - y.y) < .00001f &&
	       std::abs(x.z - y.z) < .00001f &&
	       std::abs(std::abs(dot) - 1) < .00001;
}

/// <summary>
/// 有効な剛体と、同じ物理ワールドで衝突する固体の定義を集める
/// </summary>
/// <param name="scene">姿勢とコンポーネントの参照元</param>
/// <param name="bodies">成功時に設定する箱。剛体がなければ空</param>
/// <param name="driven">経路や主人公から姿勢を受け取る箱</param>
/// <param name="invalid">失敗した物体ID</param>
void Collect(const Scene &scene, std::vector<RigidBodyDescription> &bodies,
             std::set<SceneObjectId> &driven, SceneObjectId &invalid) {
	SceneActivity::States active;
	SceneHierarchy::WorldTransforms transforms;
	if (!SceneActivity::BuildStates(scene, active) ||
	    !SceneHierarchy::BuildWorldTransforms(scene, transforms))
		throw std::runtime_error(
		    "Rigid Body hierarchy or world transform is invalid.");
	bool enabled = false;
	for (const auto &[id, o] : scene.GetObjects()) {
		if (!active.at(id) || !o.rigidBody)
			continue;
		invalid = id;
		if (!o.rigidBody->IsValid())
			throw std::runtime_error("Rigid Body settings are invalid.");
		if (!o.rigidBody->enabled)
			continue;
		enabled = true;
		if (!o.collider || o.collider->isTrigger)
			throw std::runtime_error("Rigid Body requires a non-Trigger Box "
			                         "Collider on the same object.");
		if (o.character || o.followCamera ||
		    ((o.motionPath || o.transformAnimation) &&
		     !o.rigidBody->isKinematic))
			throw std::runtime_error(
			    "Rigid Body cannot share an object with Character Controller "
			    "or Follow Camera; Motion Path and Transform Animation require "
			    "Is Kinematic.");
		// 非一様な親の下で回転すると、ローカルSRTでは剛体の姿勢を保てない。
		for (auto parent = o.parentId; parent;
		     parent = scene.FindObject(parent)->parentId) {
			const auto &scale = scene.FindObject(parent)->transform.scale;
			const float largest = (std::max)({scale.x, scale.y, scale.z});
			if (scale.x <= 0 || scale.y <= 0 || scale.z <= 0 ||
			    std::abs(scale.x - scale.y) > largest * .00001f ||
			    std::abs(scale.x - scale.z) > largest * .00001f)
				throw std::runtime_error(
				    "Rigid Body requires positive uniform scale on ancestors; "
				    "use its own Transform for non-uniform dimensions.");
		}
		Transform pose;
		if (!TransformUtils::TryFromWorldMatrix(transforms.at(id).matrix,
		                                        pose) ||
		    pose.scale.x <= 0 || pose.scale.y <= 0 || pose.scale.z <= 0)
			throw std::runtime_error(
			    "Rigid Body requires a positive world scale without shear.");
	}
	if (!enabled) {
		invalid = 0;
		return;
	}
	for (const auto &[id, o] : scene.GetObjects()) {
		if (!active.at(id) || !o.collider || o.collider->isTrigger)
			continue;
		invalid = id;
		bool path = bool(o.motionPath) || bool(o.transformAnimation),
		     character = bool(o.character), belowCharacter = false;
		bool belowRigid = false;
		for (auto parent = o.parentId; parent;
		     parent = scene.FindObject(parent)->parentId) {
			const auto &ancestor = *scene.FindObject(parent);
			path |= bool(ancestor.motionPath) ||
			        bool(ancestor.transformAnimation) ||
			        bool(ancestor.followCamera);
			belowCharacter |= bool(ancestor.character);
			belowRigid |= ancestor.rigidBody && ancestor.rigidBody->enabled;
		}
		if (belowCharacter)
			continue;
		if (belowRigid && (!o.rigidBody || !o.rigidBody->enabled))
			throw std::runtime_error(
			    "A solid Collider below a Rigid Body needs its own Rigid Body; "
			    "compound colliders are not supported yet.");
		RigidBodyDescription d;
		d.object = id;
		d.collider = *o.collider;
		d.rigidBody = o.rigidBody;
		const bool decomposed = TransformUtils::TryFromWorldMatrix(
		    transforms.at(id).matrix, d.transform);
		if (character || !decomposed || d.transform.scale.x <= 0 ||
		    d.transform.scale.y <= 0 || d.transform.scale.z <= 0) {
			AxisAlignedBox bounds;
			if (!BoxColliderUtils::GetWorldBounds(
			        *o.collider, transforms.at(id).matrix, bounds))
				throw std::runtime_error(
				    "Physics collider cannot be represented in world space.");
			d.transform = {};
			d.transform.translation = {
			    (bounds.minimum.x + bounds.maximum.x) * .5f,
			    (bounds.minimum.y + bounds.maximum.y) * .5f,
			    (bounds.minimum.z + bounds.maximum.z) * .5f};
			d.collider.center = {};
			d.collider.halfExtents = {
			    (bounds.maximum.x - bounds.minimum.x) * .5f,
			    (bounds.maximum.y - bounds.minimum.y) * .5f,
			    (bounds.maximum.z - bounds.minimum.z) * .5f};
		}
		if (character || (path && (!d.rigidBody || !d.rigidBody->enabled ||
		                           d.rigidBody->isKinematic))) {
			if (!d.rigidBody)
				d.rigidBody = RigidBodySettings{};
			d.rigidBody->enabled = true;
			d.rigidBody->isKinematic = true;
			d.rigidBody->linearVelocity = {};
			d.rigidBody->angularVelocity = {};
			driven.insert(id);
		}
		bodies.push_back(d);
	}
	if (bodies.size() > RigidBodyWorld::kMaximumBodies)
		throw std::runtime_error(
		    "A scene with Rigid Body supports at most 1024 solid Colliders.");
	invalid = 0;
}
} // namespace

bool ScenePhysics::Validate(const Scene &scene, std::string &error,
                            SceneObjectId *invalidObject) {
	SceneObjectId invalid = 0;
	if (invalidObject)
		*invalidObject = 0;
	try {
		std::vector<RigidBodyDescription> bodies;
		std::set<SceneObjectId> driven;
		Collect(scene, bodies, driven, invalid);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		if (invalidObject)
			*invalidObject = invalid;
		error = std::format("Rigid Body ID {}: {}", invalid, exception.what());
		return false;
	}
}

bool ScenePhysics::Configure(const Scene &scene, bool preserve,
                             std::string &error, SceneObjectId *invalidObject) {
	SceneObjectId invalid = 0;
	if (invalidObject)
		*invalidObject = 0;
	try {
		std::vector<RigidBodyDescription> descriptions;
		std::set<SceneObjectId> driven;
		Collect(scene, descriptions, driven, invalid);
		ScenePhysics next;
		if (descriptions.empty()) {
			*this = std::move(next);
			error.clear();
			return true;
		}
		for (const auto &d : descriptions)
			next.definitions_.emplace(d.object, d);
		next.driven_ = std::move(driven);
		const bool same =
		    preserve && world_ && definitions_.size() == descriptions.size() &&
		    std::ranges::all_of(descriptions, [&](const auto &d) {
			    auto f = definitions_.find(d.object);
			    return f != definitions_.end() && SameDefinition(f->second, d);
		    });
		if (same) {
			next.world_ = world_;
			for (const auto &d : descriptions) {
				const auto state = next.world_->GetState(d.object);
				if (!state ||
				    (!SamePose(state->transform, d.transform) &&
				     !next.world_->Teleport(d.object, d.transform, error))) {
					if (invalidObject)
						*invalidObject = d.object;
					return false;
				}
			}
		} else {
			if (preserve && world_)
				for (auto &d : descriptions) {
					const auto old = definitions_.find(d.object);
					const auto state = world_->GetState(d.object);
					if (state && old != definitions_.end() &&
					    old->second.rigidBody &&
					    old->second.rigidBody->enabled && d.rigidBody &&
					    d.rigidBody->enabled && !d.rigidBody->isKinematic &&
					    !old->second.rigidBody->isKinematic) {
						d.rigidBody->linearVelocity = state->linearVelocity;
						d.rigidBody->angularVelocity = state->angularVelocity;
						d.startSleeping = !state->active;
					}
				}
			next.world_.emplace();
			if (!next.world_->Rebuild(descriptions, error, invalidObject))
				return false;
		}
		*this = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		if (invalidObject)
			*invalidObject = invalid;
		error = std::format("Rigid Body ID {}: {}", invalid, exception.what());
		return false;
	}
}

bool ScenePhysics::Advance(Scene &scene, float seconds, std::string &error) {
	try {
		if (!std::isfinite(seconds) || seconds < 0)
			throw std::runtime_error(
			    "Physics time must be finite and non-negative.");
		if (!world_ || seconds < .000001f) {
			error.clear();
			return true;
		}
		auto next = *this;
		std::vector<RigidBodyDescription> current;
		std::set<SceneObjectId> driven;
		SceneObjectId invalid = 0;
		Collect(scene, current, driven, invalid);

		// 拡縮で形状が変わったときだけ作り直す。姿勢だけの変更はMoveKinematicで速度を持たせる。
		const bool same = next.definitions_.size() == current.size() &&
		                  std::ranges::all_of(current, [&](const auto &d) {
			                  const auto found =
			                      next.definitions_.find(d.object);
			                  return found != next.definitions_.end() &&
			                         SameDefinition(found->second, d);
		                  });
		if (!same && !next.Configure(scene, true, error))
			return false;
		const float duration =
		    (std::min)(seconds, RigidBodyWorld::kMaximumFrameSeconds);
		for (const auto &d : current) {
			if (driven.contains(d.object)) {
				if (!next.world_->MoveKinematic(d.object, d.transform, duration,
				                                error))
					return false;
			} else if (!d.rigidBody || !d.rigidBody->enabled) {
				const auto state = next.world_->GetState(d.object);
				if (!state ||
				    (!SamePose(state->transform, d.transform) &&
				     !next.world_->Teleport(d.object, d.transform, error)))
					return false;
			}
		}
		if (!next.world_->Update(duration, error))
			return false;
		std::vector<SceneObjectId> order;
		if (!SceneHierarchy::GetEvaluationOrder(scene, order))
			throw std::runtime_error("Rigid Body hierarchy is invalid.");
		for (auto id : order) {
			const auto *o = scene.FindObject(id);
			if (!o->rigidBody || !o->rigidBody->enabled ||
			    !SceneActivity::IsActive(scene, id))
				continue;
			const auto state = next.world_->GetState(id);
			if (!state || !SceneHierarchy::SetWorldPose(
			                  scene, id, state->transform.translation,
			                  state->transform.rotation))
				throw std::runtime_error(
				    "Rigid Body pose cannot be represented in the parent "
				    "hierarchy.");
		}
		*this = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool ScenePhysics::SetLinearVelocity(SceneObjectId id,
                                     const DirectX::XMFLOAT3 &velocity,
                                     std::string &error) {
	if (!world_ || driven_.contains(id)) {
		error = "Velocity requires a physical runtime and cannot override "
		        "Motion Path.";
		return false;
	}
	return world_->SetLinearVelocity(id, velocity, error);
}

bool ScenePhysics::SetAngularVelocity(SceneObjectId id,
                                      const DirectX::XMFLOAT3 &velocity,
                                      std::string &error) {
	if (!world_ || driven_.contains(id)) {
		error = "Angular velocity requires a physical runtime and cannot "
		        "override Motion Path.";
		return false;
	}
	return world_->SetAngularVelocity(id, velocity, error);
}

bool ScenePhysics::AddImpulse(SceneObjectId id,
                              const DirectX::XMFLOAT3 &impulse,
                              std::string &error) {
	if (!world_) {
		error = "Impulse requires a physical runtime.";
		return false;
	}
	return world_->AddImpulse(id, impulse, error);
}
