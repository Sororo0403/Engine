#include "gameplay/EditorGamePreview.h"

#include "gameplay/SceneCameraUtils.h"
#include "graphics/camera/Camera.h"
#include "math/TransformUtils.h"
#include <exception>
#include <utility>
#include <vector>

bool EditorGamePreview::Prepare(const Scene &source, const CameraPose &fallback,
                                SceneObjectId cameraOverride) {
	try {
		std::vector<SceneObjectId> order;
		std::string error;

		if (!SceneCameraUtils::GetFollowOrder(source, order, error)) {
			Reset();
			return false;
		}

		auto objects = source.GetObjects();

		// 追従先を先に解決し、Play開始時と同じ位置へCameraの表示物体も置く。
		for (const auto id : order) {
			auto &object = objects.at(id);
			const auto &target = objects.at(object.followCamera->target);

			auto pose = SceneCameraUtils::MakeFollowPose(
			    target.transform.translation, *object.followCamera);
			if (!SceneCameraUtils::AvoidFollowObstacles(source, id, pose) ||
			    !SceneCameraUtils::ApplyPose(pose, object.transform)) {
				Reset();
				return false;
			}
		}

		CameraPose pose = fallback;
		CameraSettings settings;
		bool primary = false;

		for (const auto &[id, object] : objects) {
			if (!object.camera || !object.camera->primary) {
				continue;
			}

			if (primary || !object.camera->IsValid() ||
			    !TransformUtils::IsValid(object.transform)) {
				Reset();
				return false;
			}

			primary = true;
			pose = SceneCameraUtils::FromTransform(object.transform);
			settings = object.camera->settings;
		}

		if (cameraOverride != 0) {
			const auto found = objects.find(cameraOverride);

			if (found == objects.end() || !found->second.camera ||
			    !found->second.camera->IsValid() ||
			    !TransformUtils::IsValid(found->second.transform)) {
				Reset();
				return false;
			}

			pose = SceneCameraUtils::FromTransform(found->second.transform);
			settings = found->second.camera->settings;
		}

		Camera validator;

		if (!validator.SetPose(pose) || !validator.SetSettings(settings)) {
			Reset();
			return false;
		}

		scene_.ReplaceObjects(std::move(objects));
		pose_ = pose;
		settings_ = settings;
		return true;
	} catch (const std::exception &) {
		Reset();
		return false;
	}
}

void EditorGamePreview::Reset() {
	scene_.Clear();
	pose_ = {};
	settings_ = {};
}

const Scene &EditorGamePreview::GetScene() const {
	return scene_;
}

const CameraPose &EditorGamePreview::GetPose() const {
	return pose_;
}

const CameraSettings &EditorGamePreview::GetSettings() const {
	return settings_;
}
