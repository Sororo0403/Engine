#include "gameplay/EditorGamePreview.h"

#include "gameplay/SceneCameraUtils.h"
#include "graphics/camera/Camera.h"
#include "math/TransformUtils.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include <exception>
#include <utility>
#include <vector>

bool EditorGamePreview::Prepare(const Scene &source, const CameraPose &fallback,
                                SceneObjectId cameraOverride) {
	try {
		Scene resolved;
		std::string error;

		if (!SceneCameraUtils::ResolveInitialScene(source, resolved, error)) {
			Reset();
			return false;
		}

		const auto &objects = resolved.GetObjects();
		CameraPose pose = fallback;
		CameraSettings settings;
		bool primary = false;

		for (const auto &[id, object] : objects) {
			if (!SceneActivity::IsActive(resolved, id) || !object.camera ||
			    !object.camera->primary) {
				continue;
			}

			if (primary || !object.camera->IsValid() ||
			    !TransformUtils::IsValid(object.transform)) {
				Reset();
				return false;
			}

			primary = true;
			SceneWorldTransform transform;

			if (!SceneHierarchy::GetWorldTransform(resolved, id, transform)) {
				Reset();
				return false;
			}

			pose = SceneCameraUtils::FromWorldTransform(transform);
			settings = object.camera->settings;
		}

		if (cameraOverride != 0) {
			const auto found = objects.find(cameraOverride);

			if (found == objects.end() ||
			    !SceneActivity::IsActive(resolved, cameraOverride) ||
			    !found->second.camera || !found->second.camera->IsValid() ||
			    !TransformUtils::IsValid(found->second.transform)) {
				Reset();
				return false;
			}

			SceneWorldTransform transform;

			if (!SceneHierarchy::GetWorldTransform(resolved, cameraOverride,
			                                       transform)) {
				Reset();
				return false;
			}

			pose = SceneCameraUtils::FromWorldTransform(transform);
			settings = found->second.camera->settings;
		}

		Camera validator;

		if (!validator.SetPose(pose) || !validator.SetSettings(settings)) {
			Reset();
			return false;
		}

		scene_.CopyFrom(resolved);
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
