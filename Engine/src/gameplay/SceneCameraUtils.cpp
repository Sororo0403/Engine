#include "gameplay/SceneCameraUtils.h"

#include "gameplay/FollowCameraSettings.h"
#include "graphics/camera/Camera.h"
#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include "physics/CollisionWorld.h"
#include "scene/Scene.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <set>
#include <utility>

/// <summary>
/// 追従対象からカメラ位置と注視点を計算する
/// </summary>
/// <param name="target">対象位置</param>
/// <param name="settings">カメラ設定</param>
/// <returns>ワールド座標のカメラ姿勢</returns>
CameraPose
SceneCameraUtils::MakeFollowPose(const DirectX::XMFLOAT3 &target,
                                 const FollowCameraSettings &settings) {
	CameraPose result;
	result.position = {target.x + settings.offset.x,
	                   target.y + settings.offset.y,
	                   target.z + settings.offset.z};
	result.target = {target.x + settings.lookOffset.x,
	                 target.y + settings.lookOffset.y,
	                 target.z + settings.lookOffset.z};
	return result;
}

bool SceneCameraUtils::AvoidFollowObstacles(const Scene &scene,
                                            SceneObjectId id,
                                            CameraPose &pose) {
	const auto *object = scene.FindObject(id);
	if (!object || !object->camera || !object->followCamera ||
	    !object->followCamera->IsValid()) {
		return false;
	}
	const auto &settings = *object->followCamera;
	if (!settings.avoidObstacles) {
		return true;
	}
	Camera validator;
	if (!validator.SetPose(pose)) {
		return false;
	}

	std::vector<SceneObjectId> ignored{settings.target};
	for (const auto &[objectId, candidate] : scene.GetObjects()) {
		if (candidate.character || candidate.followCamera) {
			ignored.push_back(objectId);
		}
	}
	CollisionWorld world;
	if (!world.Rebuild(scene, std::span<const SceneObjectId>{ignored})) {
		return false;
	}

	const float radius = settings.clearance;
	const auto boxAt = [radius](const DirectX::XMFLOAT3 &point) {
		return AxisAlignedBox{
		    {point.x - radius, point.y - radius, point.z - radius},
		    {point.x + radius, point.y + radius, point.z + radius}};
	};
	const DirectX::XMFLOAT3 movement{pose.position.x - pose.target.x,
	                                 pose.position.y - pose.target.y,
	                                 pose.position.z - pose.target.z};
	CollisionSweepResult hit;
	if (!world.Sweep(boxAt(pose.target), movement, hit)) {
		return false;
	}
	if (!hit.objectId) {
		return true;
	}

	const double distance =
	    std::hypot(double(movement.x), double(movement.y), double(movement.z));
	if (distance == 0 || hit.fraction <= 0) {
		return false;
	}
	const double magnitude = (std::max)({std::abs(double(pose.position.x)),
	                                     std::abs(double(pose.position.y)),
	                                     std::abs(double(pose.position.z))});
	const double skin =
	    (std::max)(FollowCameraSettings::kCollisionSkinWidth,
	               magnitude * std::numeric_limits<float>::epsilon() * 4);
	const double fraction = (std::max)(0.0, hit.fraction - skin / distance);
	auto adjusted = pose;
	adjusted.position = {
	    static_cast<float>(pose.target.x + movement.x * fraction),
	    static_cast<float>(pose.target.y + movement.y * fraction),
	    static_cast<float>(pose.target.z + movement.z * fraction)};
	if (!world.CanPlace(boxAt(adjusted.position)) ||
	    !validator.SetPose(adjusted)) {
		return false;
	}
	pose = adjusted;
	return true;
}

/// <summary>
/// Transformの位置と回転から、+Zを前、+Yを上とする描画姿勢を作る
/// </summary>
/// <param name="transform">カメラ物体の姿勢。拡大率は描画姿勢へ適用しない</param>
/// <returns>ワールド座標のカメラ姿勢</returns>
CameraPose SceneCameraUtils::FromTransform(const Transform &transform) {
	using namespace DirectX;
	CameraPose pose;
	pose.position = transform.translation;
	const auto q = XMLoadFloat4(&transform.rotation);
	XMFLOAT3 forward;
	XMStoreFloat3(&forward, XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q));
	XMStoreFloat3(&pose.up, XMVector3Rotate(XMVectorSet(0, 1, 0, 0), q));
	pose.target = {pose.position.x + forward.x, pose.position.y + forward.y,
	               pose.position.z + forward.z};
	return pose;
}

/// <summary>
/// 追従結果を物体のTransformへ反映する
/// </summary>
/// <param name="pose">追従結果の位置・注視点・上方向</param>
/// <param name="transform">位置と回転を更新するTransform。拡大率は保持する</param>
/// <returns>true:成功 false:描画姿勢が無効</returns>
bool SceneCameraUtils::ApplyPose(const CameraPose &pose, Transform &transform) {
	Camera validator;

	if (!validator.SetPose(pose)) {
		return false;
	}

	using namespace DirectX;
	const auto view =
	    XMMatrixLookAtLH(XMLoadFloat3(&pose.position),
	                     XMLoadFloat3(&pose.target), XMLoadFloat3(&pose.up));
	const auto world = XMMatrixInverse(nullptr, view);
	XMFLOAT4 rotation;
	XMStoreFloat4(&rotation, XMQuaternionRotationMatrix(world));

	if (!TransformUtils::TryNormalizeRotation(rotation, rotation)) {
		return false;
	}

	transform.translation = pose.position;
	transform.rotation = rotation;
	return true;
}

namespace {
/// <summary>
/// 追従元のCameraと設定を検証し、自己参照でない追従先を取得する
/// </summary>
/// <param name="scene">参照するシーン</param>
/// <param name="id">Follow CameraのID</param>
/// <returns>有効な追従先。設定や対象が無効ならnullptr</returns>
const SceneObject *FollowTarget(const Scene &scene, SceneObjectId id) {
	const auto *object = scene.FindObject(id);

	if (!object || !object->followCamera || !object->camera ||
	    object->character || !object->camera->IsValid() ||
	    !TransformUtils::IsValid(object->transform) ||
	    !object->followCamera->IsValid() ||
	    object->followCamera->target == id) {
		return nullptr;
	}

	return scene.FindObject(object->followCamera->target);
}
} // namespace

bool SceneCameraUtils::GetInitialTransform(const Scene &scene, SceneObjectId id,
                                           Transform &resultTransform) {
	const auto *object = scene.FindObject(id);

	if (!object || !object->camera || !object->camera->IsValid() ||
	    !TransformUtils::IsValid(object->transform)) {
		return false;
	}

	auto transform = object->transform;
	std::vector<SceneObjectId> chain;
	std::set<SceneObjectId> visited;
	auto currentId = id;
	const auto *current = object;

	// 再帰を使わず末端までたどり、循環があれば結果を変更しない。
	while (current->followCamera) {
		const auto *target = FollowTarget(scene, currentId);

		if (!target || !visited.insert(currentId).second) {
			return false;
		}

		chain.push_back(currentId);
		currentId = current->followCamera->target;
		current = target;
	}

	auto position = current->transform.translation;

	for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
		const auto &camera = *scene.FindObject(*it);
		auto resolved = camera.transform;
		auto pose = MakeFollowPose(position, *camera.followCamera);
		if (!AvoidFollowObstacles(scene, *it, pose) ||
		    !ApplyPose(pose, resolved)) {
			return false;
		}

		position = resolved.translation;
		transform = resolved;
	}

	resultTransform = transform;
	return true;
}

bool SceneCameraUtils::GetInitialPose(const Scene &scene, SceneObjectId id,
                                      CameraPose &pose) {
	Transform transform;

	if (!GetInitialTransform(scene, id, transform)) {
		return false;
	}

	const auto result = FromTransform(transform);
	Camera validator;

	if (!validator.SetPose(result)) {
		return false;
	}

	pose = result;
	return true;
}

bool SceneCameraUtils::CanSetFollowTarget(const Scene &scene, SceneObjectId id,
                                          SceneObjectId target) {
	if (!scene.Contains(id)) {
		return false;
	}

	// シーンの物体数を上限にして、既に存在する循環でも必ず停止する。
	for (std::size_t count = 0; count < scene.GetObjects().size(); ++count) {
		if (target == 0) {
			return true;
		}

		if (target == id) {
			return false;
		}

		const auto *object = scene.FindObject(target);

		if (!object) {
			return false;
		}

		if (!object->followCamera) {
			return true;
		}

		target = object->followCamera->target;
	}

	return false;
}

bool SceneCameraUtils::GetFollowOrder(const Scene &scene,
                                      std::vector<SceneObjectId> &order,
                                      std::string &error,
                                      SceneObjectId *invalidObject) {
	if (invalidObject) {
		*invalidObject = 0;
	}

	error.clear();
	std::map<SceneObjectId, std::vector<SceneObjectId>> dependents;
	std::set<SceneObjectId> ready;
	std::size_t count = 0;

	for (const auto &[id, object] : scene.GetObjects()) {
		if (!object.followCamera) {
			continue;
		}

		const auto *target = FollowTarget(scene, id);

		if (!target) {
			error =
			    std::format("Follow Camera requires a valid Camera, a target "
			                "in this scene, "
			                "and no Character Controller on the same object. "
			                "Object: \"{}\" (ID {}). Target ID: {}.",
			                object.name.empty() ? "Object" : object.name, id,
			                object.followCamera->target);

			if (invalidObject) {
				*invalidObject = id;
			}

			return false;
		}

		++count;

		if (target->followCamera) {
			dependents[object.followCamera->target].push_back(id);
		} else {
			ready.insert(id);
		}
	}

	std::vector<SceneObjectId> result;
	result.reserve(count);

	while (!ready.empty()) {
		const auto id = *ready.begin();
		ready.erase(ready.begin());
		result.push_back(id);

		if (const auto found = dependents.find(id); found != dependents.end()) {
			ready.insert(found->second.begin(), found->second.end());
		}
	}

	if (result.size() != count) {
		// 更新順に入らなかった物体には循環の後続も含まれる。
		// 追従先をたどり、再訪した物体だけを循環内の例として通知する。
		std::set<SceneObjectId> resolved(result.begin(), result.end());
		SceneObjectId cycle = 0;
		for (const auto &[id, object] : scene.GetObjects()) {
			if (object.followCamera && !resolved.contains(id)) {
				cycle = id;
				break;
			}
		}
		std::set<SceneObjectId> visited;
		while (visited.insert(cycle).second) {
			cycle = scene.FindObject(cycle)->followCamera->target;
		}
		const auto &object = *scene.FindObject(cycle);
		error =
		    std::format("Follow Camera targets form a cycle. Choose targets "
		                "without a circular chain. "
		                "Cycle contains object: \"{}\" (ID {}).",
		                object.name.empty() ? "Object" : object.name, cycle);

		if (invalidObject) {
			*invalidObject = cycle;
		}

		return false;
	}

	order = std::move(result);
	return true;
}

bool SceneCameraUtils::MatchFollowPose(const CameraPose &pose,
                                       const DirectX::XMFLOAT3 &target,
                                       FollowCameraSettings &settings) {
	Camera validator;

	if (!settings.IsValid() || !MathUtils::IsFinite(target) ||
	    !validator.SetPose(pose)) {
		return false;
	}

	using namespace DirectX;
	XMFLOAT3 direction;
	XMStoreFloat3(&direction, XMVector3Normalize(XMVectorSubtract(
	                              XMLoadFloat3(&pose.target),
	                              XMLoadFloat3(&pose.position))));
	const double horizontal =
	    std::hypot(double(direction.x), double(direction.z));

	if (horizontal == 0) {
		return false;
	}

	auto result = settings;
	result.offset = {pose.position.x - target.x, pose.position.y - target.y,
	                 pose.position.z - target.z};
	double maximumDistance = (std::numeric_limits<double>::max)();
	const float offsets[]{result.offset.x, result.offset.y, result.offset.z};
	const float directions[]{direction.x, direction.y, direction.z};

	// 設定可能なボックスから視線が出るまでの距離を求める。
	for (unsigned int i = 0; i < 3; ++i) {
		if (!std::isfinite(offsets[i]) ||
		    std::abs(offsets[i]) > FollowCameraSettings::kMaximumOffset) {
			return false;
		}

		if (directions[i] != 0) {
			const double boundary = directions[i] > 0
			                            ? FollowCameraSettings::kMaximumOffset
			                            : -FollowCameraSettings::kMaximumOffset;
			maximumDistance =
			    (std::min)(maximumDistance,
			               (boundary - offsets[i]) / directions[i]);
		}
	}

	const double originalDistance =
	    std::hypot(double(settings.lookOffset.x) - settings.offset.x,
	               double(settings.lookOffset.y) - settings.offset.y,
	               double(settings.lookOffset.z) - settings.offset.z);
	// 最小水平距離には余裕を持たせ、設定の境界では収まる距離へ短縮する。
	const double minimumDistance =
	    std::sqrt(FollowCameraSettings::kMinimumHorizontalDistanceSquared) /
	    horizontal;
	const double distance =
	    (std::min)(maximumDistance,
	               (std::max)(originalDistance, minimumDistance * 2));

	if (!std::isfinite(distance) || distance <= 0) {
		return false;
	}

	result.lookOffset = {
	    static_cast<float>(result.offset.x + direction.x * distance),
	    static_cast<float>(result.offset.y + direction.y * distance),
	    static_cast<float>(result.offset.z + direction.z * distance)};

	if (!result.IsValid()) {
		return false;
	}

	settings = result;
	return true;
}
