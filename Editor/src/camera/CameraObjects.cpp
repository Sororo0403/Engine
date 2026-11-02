#include "camera/CameraObjects.h"

#include "gameplay/SceneCameraUtils.h"

std::optional<SceneObject>
CameraObjects::CreateFromView(const CameraPose &pose,
                              const CameraSettings &settings) {
	if (!settings.IsValid()) {
		return std::nullopt;
	}

	SceneObject object;
	object.name = "Camera";
	object.camera = CameraComponent{true, settings};

	if (!SceneCameraUtils::ApplyPose(pose, object.transform)) {
		return std::nullopt;
	}

	return object;
}
