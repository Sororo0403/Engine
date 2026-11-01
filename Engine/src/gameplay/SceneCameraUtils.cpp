#include "gameplay/SceneCameraUtils.h"

#include "gameplay/FollowCameraSettings.h"
#include "graphics/camera/Camera.h"
#include "math/TransformUtils.h"
#include "scene/Scene.h"

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

bool SceneCameraUtils::GetInitialPose(const Scene &scene, SceneObjectId id,
                                      CameraPose &pose) {
	const auto *object = scene.FindObject(id);

	if (!object || !object->camera || !object->camera->IsValid() ||
	    !TransformUtils::IsValid(object->transform)) {
		return false;
	}

	auto transform = object->transform;

	if (object->followCamera) {
		const auto &settings = *object->followCamera;
		const auto *target = scene.FindObject(settings.target);

		if (object->character || settings.target == id || !target ||
		    !settings.IsValid() ||
		    !ApplyPose(MakeFollowPose(target->transform.translation, settings),
		               transform)) {
			return false;
		}
	}

	const auto result = FromTransform(transform);
	Camera validator;

	if (!validator.SetPose(result)) {
		return false;
	}

	pose = result;
	return true;
}
