#include "gameplay/FollowCameraPreview.h"

#include "gameplay/SceneCameraUtils.h"
#include "math/MathUtils.h"
#include "scene/Scene.h"
#include "scene/SceneRenderer.h"

bool FollowCameraPreview::BuildLines(const Scene &scene, SceneObjectId id,
                                     Lines &lines) const {
	const auto *object = scene.FindObject(id);
	CameraPose pose;

	if (!object || !object->followCamera ||
	    !SceneCameraUtils::GetInitialPose(scene, id, pose)) {
		return false;
	}

	const auto &settings = *object->followCamera;
	const auto &target = *scene.FindObject(settings.target);
	auto position = target.transform.translation;

	if (target.followCamera) {
		CameraPose targetPose;

		if (!SceneCameraUtils::GetInitialPose(scene, settings.target,
		                                      targetPose)) {
			return false;
		}

		position = targetPose.position;
	}

	// GetInitialPoseの注視点は単位方向なので、設定したLook Offsetの実際の位置を求める。
	pose = SceneCameraUtils::MakeFollowPose(position, settings);
	if (!SceneCameraUtils::AvoidFollowObstacles(scene, id, pose)) {
		return false;
	}
	Lines result{};
	result[0] = {position, pose.position, targetColor_};
	result[1] = {pose.position, pose.target, lookColor_};
	const std::array<DirectX::XMFLOAT3, 3> axes{{
	    {kMarkerRadius, 0, 0},
	    {0, kMarkerRadius, 0},
	    {0, 0, kMarkerRadius},
	}};

	for (std::size_t i = 0; i < axes.size(); ++i) {
		const auto &axis = axes[i];
		result[2 + i] = {
		    {position.x - axis.x, position.y - axis.y, position.z - axis.z},
		    {position.x + axis.x, position.y + axis.y, position.z + axis.z},
		    targetColor_};
		result[5 + i] = {{pose.target.x - axis.x, pose.target.y - axis.y,
		                  pose.target.z - axis.z},
		                 {pose.target.x + axis.x, pose.target.y + axis.y,
		                  pose.target.z + axis.z},
		                 lookColor_};
	}

	for (const auto &line : result) {
		if (!MathUtils::IsFinite(line.start) ||
		    !MathUtils::IsFinite(line.end)) {
			return false;
		}
	}

	lines = result;
	return true;
}

bool FollowCameraPreview::Update(SceneRenderer &renderer, const Scene &scene,
                                 SceneObjectId id) const {
	Lines lines;

	if (!BuildLines(scene, id, lines)) {
		return true;
	}

	for (const auto &line : lines) {
		if (!renderer.AddDebugLine(line)) {
			return false;
		}
	}

	return true;
}
