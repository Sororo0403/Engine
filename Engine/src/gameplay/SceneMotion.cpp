#include "gameplay/SceneMotion.h"

#include "gameplay/CharacterController.h"
#include "physics/ColliderUtils.h"
#include "physics/CollisionWorld.h"
#include "physics/TriggerWorld.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <vector>

namespace {
/// <summary>
/// ワールド位置の差を求める
/// </summary>
/// <param name="a">移動後</param>
/// <param name="b">移動前</param>
/// <returns>移動量</returns>
DirectX::XMFLOAT3 Difference(const DirectX::XMFLOAT3 &a,
                             const DirectX::XMFLOAT3 &b) {
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}

/// <summary>
/// 動かす枝の固体が、移動前の位置から障害物を通過しないか調べる
/// </summary>
/// <param name="before">移動前のシーン</param>
/// <param name="after">移動後の候補</param>
/// <param name="branch">今回動かす枝</param>
/// <param name="characterBranch">固体の移動処理から除く主人公の枝</param>
/// <param name="obstacles">今回の枝と、運ぶ主人公を除いた衝突ワールド</param>
/// <param name="blocked">成功時に設定する衝突による停止</param>
/// <returns>true:検索成功 false:無効な形状・階層</returns>
bool CheckBranch(const Scene &before, const Scene &after,
                 std::span<const SceneObjectId> branch,
                 std::span<const SceneObjectId> characterBranch,
                 const CollisionWorld &obstacles, bool &blocked) {
	blocked = false;

	for (auto id : branch) {
		const auto &object = *before.FindObject(id);
		if (object.rigidBody && object.rigidBody->enabled &&
		    !object.rigidBody->isKinematic)
			continue;
		if (!object.collider || object.collider->isTrigger ||
		    !SceneActivity::IsActive(before, id) ||
		    std::ranges::find(characterBranch, id) != characterBranch.end())
			continue;
		SceneWorldTransform oldPose, newPose;
		if (!SceneHierarchy::GetWorldTransform(before, id, oldPose) ||
		    !SceneHierarchy::GetWorldTransform(after, id, newPose))
			return false;
		CollisionSweepResult hit;
		if (!obstacles.Sweep(
		        *object.collider, oldPose.matrix,
		        Difference(newPose.GetPosition(), oldPose.GetPosition()), hit))
			return false;
		if (hit.objectId && hit.fraction < 1.0) {
			blocked = true;
			return true;
		}
	}

	return true;
}

/// <summary>
/// 足元の面から小さな離隔を保ち、丸め誤差による乗客と床の食い込みを防ぐ
/// </summary>
/// <param name="scene">乗客を移した候補</param>
/// <param name="character">主人公のID</param>
/// <param name="ground">足元の固体ID</param>
/// <param name="pose">成功時に更新する乗客のワールド姿勢</param>
/// <returns>true:配置成功 false:形状や階層が無効、または離隔を表現できない</returns>
bool SeparatePassenger(Scene &scene, SceneObjectId character,
                       SceneObjectId ground, SceneWorldTransform &pose) {
	constexpr float clearance = 0.0002f;
	SceneWorldTransform floorPose;
	AxisAlignedBox floor;
	if (!SceneHierarchy::GetWorldTransform(scene, ground, floorPose) ||
	    !ColliderUtils::GetWorldBounds(*scene.FindObject(ground)->collider,
	                                   floorPose.matrix, floor))
		return false;
	for (int attempt = 0; attempt < 4; ++attempt) {
		AxisAlignedBox body;
		if (!ColliderUtils::GetWorldBounds(
		        *scene.FindObject(character)->collider, pose.matrix, body))
			return false;
		const double gap = double(body.minimum.y) - double(floor.maximum.y);
		if (gap >= clearance)
			return true;
		auto position = pose.GetPosition();
		position.y =
		    (std::max)(float(double(position.y) + double(clearance) - gap),
		               std::nextafter(
		                   position.y,
		                   (std::numeric_limits<float>::infinity)()));
		if (!SceneHierarchy::SetWorldPosition(scene, character, position) ||
		    !SceneHierarchy::GetWorldTransform(scene, character, pose))
			return false;
	}

	return false;
}
} // namespace

bool SceneMotion::Configure(const Scene &scene, bool preserve) {
	std::map<SceneObjectId, State> candidate;

	for (const auto &[id, object] : scene.GetObjects()) {
		if (!object.motionPath)
			continue;
		if (!object.motionPath->IsValid()) {
			if (SceneActivity::IsActive(scene, id))
				return false;
			continue;
		}

		State state{object.transform.translation, 0,
		            object.motionPath->playOnStart ? Playback::FORWARD
		                                           : Playback::PAUSE};
		if (const auto found = states_.find(id);
		    preserve && found != states_.end()) {
			state = found->second;
			DirectX::XMFLOAT3 offset;
			if (!object.motionPath->Sample(state.seconds, offset))
				return false;
			state.origin = Difference(object.transform.translation, offset);
		}
		candidate.emplace(id, state);
	}

	states_.swap(candidate);
	return true;
}

bool SceneMotion::SetPlayback(SceneObjectId id, Playback playback) {
	const auto state = states_.find(id);
	if (state == states_.end())
		return false;
	state->second.playback = playback;
	return true;
}

bool SceneMotion::Advance(Scene &scene, float seconds,
                          CharacterController *controller,
                          SceneObjectId character, TriggerWorld *triggers,
                          std::vector<TriggerEvent> *events) {
	if (!std::isfinite(seconds) || seconds < 0 ||
	    bool(controller) != bool(character) || bool(triggers) != bool(events))
		return false;
	if (seconds == 0 || states_.empty())
		return true;
	std::vector<SceneObjectId> order, characterBranch;
	if (!SceneHierarchy::GetEvaluationOrder(scene, order) ||
	    (character &&
	     !SceneHierarchy::GetSubtree(scene, character, characterBranch)))
		return false;
	double remaining = (std::min)(seconds, kMaximumFrameSeconds);

	while (remaining > 0) {
		double step = (std::min)(remaining, kMaximumStep);
		// 曲がり角や往復端で移動を区切り、角を横切る近道を作らない。
		for (const auto &[id, state] : states_) {
			if (state.playback == Playback::PAUSE ||
			    !SceneActivity::IsActive(scene, id))
				continue;
			const auto &path = *scene.FindObject(id)->motionPath;
			const double boundary = path.GetTimeToBoundary(
			    state.seconds, state.playback == Playback::REVERSE);
			if (boundary > 1e-10)
				step = (std::min)(step, boundary);
		}

		for (const auto id : order) {
			auto found = states_.find(id);
			if (found == states_.end() ||
			    found->second.playback == Playback::PAUSE ||
			    !SceneActivity::IsActive(scene, id))
				continue;
			const auto &settings = *scene.FindObject(id)->motionPath;
			if (!settings.IsValid())
				return false;
			auto state = found->second;
			const auto duration = settings.GetDuration();
			const bool reverse = state.playback == Playback::REVERSE;
			const double nextTime = state.seconds + (reverse ? -step : step);

			if (settings.mode == MotionPathComponent::Mode::ONCE) {
				state.seconds = (std::clamp)(nextTime, 0., duration);
				if (reverse ? state.seconds == 0 : state.seconds == duration)
					state.playback = Playback::PAUSE;
			} else {
				state.seconds = std::fmod(nextTime, duration);
				if (state.seconds < 0)
					state.seconds += duration;
			}

			DirectX::XMFLOAT3 offset;
			if (!settings.Sample(state.seconds, offset))
				return false;
			const DirectX::XMFLOAT3 position{state.origin.x + offset.x,
			                                 state.origin.y + offset.y,
			                                 state.origin.z + offset.z};
			const auto current = scene.FindObject(id)->transform.translation;

			if (position.x == current.x && position.y == current.y &&
			    position.z == current.z) {
				found->second = state;
				continue;
			}

			Scene candidate;
			candidate.CopyFrom(scene);
			candidate.FindObject(id)->transform.translation = position;
			std::vector<SceneObjectId> branch;
			if (!SceneHierarchy::GetSubtree(scene, id, branch))
				return false;
			const auto inBranch = [&](SceneObjectId object) {
				return std::ranges::find(branch, object) != branch.end();
			};
			SceneObjectId ground = 0;

			if (controller && controller->IsGrounded()) {
				CollisionWorld oldWorld;
				if (!oldWorld.Rebuild(scene, characterBranch) ||
				    !controller->FindGroundObject(oldWorld, ground))
					return false;
			}

			const bool carried = controller && (inBranch(character) ||
			                                    (ground && inBranch(ground)));
			auto ignored = branch;
			if (carried)
				ignored.insert(ignored.end(), characterBranch.begin(),
				               characterBranch.end());
			// 動的な箱は剛体ワールド側で移動床から力を受ける。
			for (const auto &[bodyId, body] : scene.GetObjects())
				if (body.rigidBody && body.rigidBody->enabled &&
				    !body.rigidBody->isKinematic)
					ignored.push_back(bodyId);
			CollisionWorld obstacles;
			if (!obstacles.Rebuild(scene, ignored))
				return false;
			bool blocked = false;
			if (!CheckBranch(scene, candidate, branch, characterBranch,
			                 obstacles, blocked))
				return false;
			if (blocked)
				continue;
			std::optional<CharacterController> passenger;

			if (controller) {
				SceneWorldTransform beforePose, afterPose;
				if (!SceneHierarchy::GetWorldTransform(scene, character,
				                                       beforePose) ||
				    !SceneHierarchy::GetWorldTransform(candidate, character,
				                                       afterPose))
					return false;
				if (carried && ground && inBranch(ground)) {
					SceneWorldTransform oldGround, newGround;
					if (!SceneHierarchy::GetWorldTransform(scene, ground,
					                                       oldGround) ||
					    !SceneHierarchy::GetWorldTransform(candidate, ground,
					                                       newGround))
						return false;
					const auto delta = Difference(newGround.GetPosition(),
					                              oldGround.GetPosition());
					const auto old = beforePose.GetPosition();
					if (!SceneHierarchy::SetWorldPosition(candidate, character,
					                                      {old.x + delta.x,
					                                       old.y + delta.y,
					                                       old.z + delta.z}) ||
					    !SceneHierarchy::GetWorldTransform(candidate, character,
					                                       afterPose))
						return false;
				}

				const auto &collider = *scene.FindObject(character)->collider;
				if (carried && ground && inBranch(ground) &&
				    collider.shape == Collider::Shape::BOX &&
				    !SeparatePassenger(candidate, character, ground, afterPose))
					continue;
				if (carried) {
					CollisionSweepResult hit;
					if (!obstacles.Sweep(collider, beforePose.matrix,
					                     Difference(afterPose.GetPosition(),
					                                beforePose.GetPosition()),
					                     hit))
						return false;
					if (hit.objectId && hit.fraction < 1.0)
						continue;
				}

				CollisionWorld finalWorld;
				if (!finalWorld.Rebuild(candidate, characterBranch))
					return false;
				passenger = *controller;
				if (!passenger->SynchronizeWorldMatrix(afterPose.matrix,
				                                       finalWorld) ||
				    !passenger->RefreshContacts(finalWorld))
					continue;
			}

			if (triggers) {
				if (!triggers->Update(candidate))
					return false;
				for (const auto &event : triggers->GetEvents()) {
					if (event.type != TriggerEvent::Type::STAY)
						events->push_back(event);
				}
			}

			scene.CopyFrom(candidate);
			if (passenger)
				*controller = *passenger;
			found->second = state;
		}

		remaining = (std::max)(0.0, remaining - step);
	}

	return true;
}
