#include "gameplay/SceneCameraUtils.h"

#include "gameplay/FollowCameraSettings.h"
#include "graphics/camera/Camera.h"
#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include "physics/CollisionWorld.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <limits>
#include <map>
#include <set>
#include <string_view>
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

CameraPose
SceneCameraUtils::FromWorldTransform(const SceneWorldTransform &world) {
	Transform transform;
	transform.translation = world.GetPosition();
	transform.rotation = world.rotation;
	return FromTransform(transform);
}

bool SceneCameraUtils::ApplyWorldPose(Scene &scene, SceneObjectId id,
                                      const CameraPose &pose) {
	Transform transform;
	return ApplyPose(pose, transform) &&
	       SceneHierarchy::SetWorldPose(scene, id, transform.translation,
	                                    transform.rotation);
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
	if (!object || !object->camera || !object->camera->IsValid()) {
		return false;
	}
	// 問い合わせたカメラの祖先と追従先だけを取り出し、無関係な未設定Cameraと分離する。
	std::map<SceneObjectId, SceneObject> objects;
	std::vector<SceneObjectId> pending{id};
	while (!pending.empty()) {
		const auto current = pending.back();
		pending.pop_back();
		if (current == 0 || objects.contains(current)) {
			continue;
		}
		const auto *node = scene.FindObject(current);
		if (!node) {
			return false;
		}
		objects.emplace(current, *node);
		pending.push_back(node->parentId);
		if (node->followCamera) {
			pending.push_back(node->followCamera->target);
		}
	}
	Scene source, resolved;
	try {
		source.ReplaceObjects(std::move(objects));
	} catch (const std::exception &) {
		return false;
	}
	std::string error;
	SceneWorldTransform world;
	Transform transform;
	if (!ResolveInitialScene(source, resolved, error) ||
	    !SceneHierarchy::GetWorldTransform(resolved, id, world) ||
	    !ApplyPose(FromWorldTransform(world), transform)) {
		return false;
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
	pose = FromTransform(transform);
	return true;
}

bool SceneCameraUtils::CanSetFollowTarget(const Scene &scene, SceneObjectId id,
                                          SceneObjectId target) {
	if (!scene.Contains(id)) {
		return false;
	}
	std::vector<SceneObjectId> pending{target};
	std::set<SceneObjectId> visited;
	while (!pending.empty()) {
		const auto current = pending.back();
		pending.pop_back();
		if (current == id) {
			return false;
		}
		if (current == 0 || !visited.insert(current).second) {
			continue;
		}
		const auto *node = scene.FindObject(current);
		if (!node) {
			return false;
		}
		pending.push_back(node->parentId);
		if (node->followCamera) {
			pending.push_back(node->followCamera->target);
		}
	}
	// 対象側に既存の循環があっても許可しない。依存先だけの部分グラフを検証する。
	std::map<SceneObjectId, std::set<SceneObjectId>> dependencies;
	for (const auto current : visited) {
		const auto &node = *scene.FindObject(current);
		if (node.parentId)
			dependencies[current].insert(node.parentId);
		if (node.followCamera && node.followCamera->target)
			dependencies[current].insert(node.followCamera->target);
		dependencies.try_emplace(current);
	}
	while (!dependencies.empty()) {
		auto ready = std::find_if(
		    dependencies.begin(), dependencies.end(),
		    [](const auto &entry) { return entry.second.empty(); });
		if (ready == dependencies.end())
			return false;
		const auto completed = ready->first;
		dependencies.erase(ready);
		for (auto &[key, values] : dependencies) {
			(void)key;
			values.erase(completed);
		}
	}
	return true;
}

bool SceneCameraUtils::GetUpdateOrder(const Scene &scene,
                                      std::vector<SceneObjectId> &order,
                                      std::string &error,
                                      SceneObjectId *invalidObject) {
	if (invalidObject)
		*invalidObject = 0;
	error.clear();
	const auto fail = [&](SceneObjectId id, std::string_view reason) {
		const auto *object = scene.FindObject(id);
		error = std::format(
		    "{} Object: \"{}\" (ID {}).", reason,
		    object && !object->name.empty() ? object->name : "Object", id);
		if (invalidObject)
			*invalidObject = id;
		return false;
	};
	std::map<SceneObjectId, std::set<SceneObjectId>> dependencies;
	std::map<SceneObjectId, std::vector<SceneObjectId>> dependents;
	std::set<SceneObjectId> ready;
	for (const auto &[id, object] : scene.GetObjects()) {
		auto &values = dependencies[id];
		if (object.parentId) {
			if (!scene.Contains(object.parentId))
				return fail(id, "The parent does not exist.");
			values.insert(object.parentId);
		}
		if (object.followCamera) {
			if (!FollowTarget(scene, id))
				return fail(id,
				            "Follow Camera requires a valid Camera, target, "
				            "and no Character Controller on the same object.");
			values.insert(object.followCamera->target);
		}
		if (values.empty())
			ready.insert(id);
		for (auto dependency : values)
			dependents[dependency].push_back(id);
	}
	std::vector<SceneObjectId> result;
	result.reserve(dependencies.size());
	while (!ready.empty()) {
		const auto id = *ready.begin();
		ready.erase(ready.begin());
		result.push_back(id);
		for (const auto child : dependents[id]) {
			auto &values = dependencies.at(child);
			values.erase(id);
			if (values.empty())
				ready.insert(child);
		}
		dependencies.erase(id);
	}
	if (!dependencies.empty()) {
		// 未解決の依存先をたどり、循環そのものに含まれる物体を通知する。
		auto current = dependencies.begin()->first;
		std::set<SceneObjectId> visited;
		while (visited.insert(current).second)
			current = *dependencies.at(current).begin();
		return fail(current,
		            "Parent and Follow Camera dependencies form a cycle.");
	}
	order.swap(result);
	return true;
}

bool SceneCameraUtils::GetFollowOrder(const Scene &scene,
                                      std::vector<SceneObjectId> &order,
                                      std::string &error,
                                      SceneObjectId *invalidObject) {
	std::vector<SceneObjectId> result;
	if (!GetUpdateOrder(scene, result, error, invalidObject))
		return false;
	std::erase_if(result, [&](SceneObjectId id) {
		return !scene.FindObject(id)->followCamera;
	});
	order.swap(result);
	return true;
}

bool SceneCameraUtils::ResolveInitialScene(const Scene &source, Scene &result,
                                           std::string &error,
                                           SceneObjectId *invalidObject) {
	std::vector<SceneObjectId> order;
	if (!GetUpdateOrder(source, order, error, invalidObject))
		return false;
	Scene working;
	working.ReplaceObjects(source.GetObjects());
	for (const auto id : order) {
		const auto &object = *working.FindObject(id);
		if (!object.followCamera)
			continue;
		SceneWorldTransform target;
		if (!SceneHierarchy::GetWorldTransform(
		        working, object.followCamera->target, target)) {
			error = std::format(
			    "Follow Camera target transform is invalid. Object ID: {}.",
			    id);
			if (invalidObject)
				*invalidObject = id;
			return false;
		}
		auto pose = MakeFollowPose(target.GetPosition(), *object.followCamera);
		if (!AvoidFollowObstacles(working, id, pose) ||
		    !ApplyWorldPose(working, id, pose)) {
			error = std::format("Follow Camera pose is invalid. Object ID: {}.",
			                    id);
			if (invalidObject)
				*invalidObject = id;
			return false;
		}
	}
	SceneHierarchy::WorldTransforms transforms;
	if (!SceneHierarchy::BuildWorldTransforms(working, transforms)) {
		error = "World transforms cannot be represented.";
		return false;
	}
	result.ReplaceObjects(working.GetObjects());
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
