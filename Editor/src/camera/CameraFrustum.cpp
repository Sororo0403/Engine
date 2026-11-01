#include "camera/CameraFrustum.h"

#include "gameplay/SceneCameraUtils.h"
#include "graphics/camera/Camera.h"
#include "math/MathUtils.h"
#include "scene/Scene.h"
#include "scene/SceneRenderer.h"

bool CameraFrustum::BuildLines(const Scene &scene, SceneObjectId id, int width,
                               int height, Lines &lines) const {
	CameraPose pose;

	if (!SceneCameraUtils::GetInitialPose(scene, id, pose)) {
		return false;
	}

	const auto &settings = scene.FindObject(id)->camera->settings;
	Camera camera;

	if (!camera.SetPose(pose) || !camera.SetSettings(settings) ||
	    !camera.Update(width, height)) {
		return false;
	}

	using namespace DirectX;
	const auto world =
	    XMMatrixInverse(nullptr, XMLoadFloat4x4(&camera.GetViewMatrix()));
	const auto &projection = camera.GetProjectionMatrix();
	std::array<XMFLOAT3, 8> corners;

	// 左右・上下・Near/Farを各ビットに対応させる。
	// 逆投影のFar側の丸め誤差を避け、指定距離と投影行列の拡大率から頂点を求める。
	for (unsigned int i = 0; i < corners.size(); ++i) {
		const float distance = (i & 4) ? settings.farClip : settings.nearClip;
		const float halfWidth = distance / projection._11;
		const float halfHeight = distance / projection._22;
		const auto local =
		    XMVectorSet((i & 1) ? halfWidth : -halfWidth,
		                (i & 2) ? halfHeight : -halfHeight, distance, 1);
		XMStoreFloat3(&corners[i], XMVector3TransformCoord(local, world));

		if (!MathUtils::IsFinite(corners[i])) {
			return false;
		}
	}

	Lines result{};
	std::size_t index = 0;

	for (unsigned int i = 0; i < corners.size(); ++i) {
		for (unsigned int axis : {1u, 2u, 4u}) {
			if ((i & axis) == 0) {
				result[index++] = {corners[i], corners[i | axis], color_};
			}
		}
	}

	lines = result;
	return true;
}

bool CameraFrustum::Update(SceneRenderer &renderer, const Scene &scene,
                           SceneObjectId id, int width, int height) const {
	Lines lines;

	if (!BuildLines(scene, id, width, height, lines)) {
		return true;
	}

	for (const auto &line : lines) {
		if (!renderer.AddDebugLine(line)) {
			return false;
		}
	}

	return true;
}
