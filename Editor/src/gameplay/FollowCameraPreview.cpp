#include "gameplay/FollowCameraPreview.h"

#include "gameplay/SceneCameraUtils.h"
#include "graphics/camera/Camera.h"
#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include "physics/AxisAlignedBox.h"
#include "physics/ColliderBounds.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneRenderer.h"
#include <algorithm>
#include <array>
#include <utility>

bool FollowCameraPreview::BuildLines(const Scene &scene, SceneObjectId id,
                                     Lines &lines) const {
	const auto *object = scene.FindObject(id);
	if (!object || !object->camera || !object->camera->IsValid() ||
	    !object->followCamera || !object->followCamera->IsValid() ||
	    object->character || !TransformUtils::IsValid(object->transform)) {
		return false;
	}

	const auto &settings = *object->followCamera;
	const auto *target = scene.FindObject(settings.target);

	if (!target || settings.target == id ||
	    !TransformUtils::IsValid(target->transform)) {
		return false;
	}

	SceneWorldTransform targetWorld;
	if (!SceneHierarchy::GetWorldTransform(scene, settings.target, targetWorld))
		return false;
	auto position = targetWorld.GetPosition();

	if (target->followCamera) {
		CameraPose targetPose;

		if (!SceneCameraUtils::GetInitialPose(scene, settings.target,
		                                      targetPose)) {
			return false;
		}

		position = targetPose.position;
	}

	const auto requested = SceneCameraUtils::MakeFollowPose(position, settings);
	Camera validator;

	if (!validator.SetPose(requested)) {
		return false;
	}

	auto pose = requested;
	const bool available =
	    SceneCameraUtils::AvoidFollowObstacles(scene, id, pose);
	const auto linkColor = available ? targetColor_ : blockedColor_;
	const auto lookColor = available ? lookColor_ : blockedColor_;
	Lines result;
	result.reserve(kMaximumLineCount);
	result.push_back({position, pose.position, linkColor});
	result.push_back({pose.position, pose.target, lookColor});
	const std::array<DirectX::XMFLOAT3, 3> axes{{
	    {kMarkerRadius, 0, 0},
	    {0, kMarkerRadius, 0},
	    {0, 0, kMarkerRadius},
	}};

	for (const auto &axis : axes) {
		result.push_back(
		    {{position.x - axis.x, position.y - axis.y, position.z - axis.z},
		     {position.x + axis.x, position.y + axis.y, position.z + axis.z},
		     targetColor_});
	}

	for (const auto &axis : axes) {
		result.push_back({{pose.target.x - axis.x, pose.target.y - axis.y,
		                   pose.target.z - axis.z},
		                  {pose.target.x + axis.x, pose.target.y + axis.y,
		                   pose.target.z + axis.z},
		                  lookColor});
	}

	if (settings.avoidObstacles) {
		const auto addBox = [&](const DirectX::XMFLOAT3 &center,
		                        const DirectX::XMFLOAT4 &color) {
			const float radius = settings.clearance;
			const AxisAlignedBox box{
			    {center.x - radius, center.y - radius, center.z - radius},
			    {center.x + radius, center.y + radius, center.z + radius}};
			ColliderBounds::Lines edges;

			if (!ColliderBounds{}.BuildLines(box, edges)) {
				return false;
			}

			for (auto &edge : edges) {
				edge.color = color;
				result.push_back(edge);
			}

			return true;
		};
		const auto color = available ? clearanceColor_ : blockedColor_;

		if (!addBox(pose.target, color) || !addBox(pose.position, color)) {
			return false;
		}

		if (available && (pose.position.x != requested.position.x ||
		                  pose.position.y != requested.position.y ||
		                  pose.position.z != requested.position.z)) {
			if (!addBox(requested.position, requestedColor_)) {
				return false;
			}

			result.push_back(
			    {pose.position, requested.position, requestedColor_});
		}
	}

	for (const auto &line : result) {
		if (!MathUtils::IsFinite(line.start) ||
		    !MathUtils::IsFinite(line.end)) {
			return false;
		}
	}

	lines = std::move(result);
	return true;
}

bool FollowCameraPreview::GetFrameBounds(
    const Scene &scene, SceneObjectId id,
    std::array<DirectX::XMFLOAT3, 8> &corners) const {
	Lines lines;

	if (!BuildLines(scene, id, lines) || lines.empty()) {
		return false;
	}

	auto minimum = lines.front().start;
	auto maximum = minimum;

	for (const auto &line : lines) {
		for (const auto &point : {line.start, line.end}) {
			minimum.x = (std::min)(minimum.x, point.x);
			minimum.y = (std::min)(minimum.y, point.y);
			minimum.z = (std::min)(minimum.z, point.z);
			maximum.x = (std::max)(maximum.x, point.x);
			maximum.y = (std::max)(maximum.y, point.y);
			maximum.z = (std::max)(maximum.z, point.z);
		}
	}

	if (minimum.x >= maximum.x || minimum.y >= maximum.y ||
	    minimum.z >= maximum.z) {
		return false;
	}

	std::array<DirectX::XMFLOAT3, 8> result;

	for (std::size_t i = 0; i < result.size(); ++i) {
		result[i] = {(i & 1) ? maximum.x : minimum.x,
		             (i & 2) ? maximum.y : minimum.y,
		             (i & 4) ? maximum.z : minimum.z};
	}

	corners = result;
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
