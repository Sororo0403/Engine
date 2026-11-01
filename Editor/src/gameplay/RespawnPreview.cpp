#include "gameplay/RespawnPreview.h"

#include "math/MathUtils.h"
#include "scene/SceneObject.h"
#include "scene/SceneRenderer.h"

bool RespawnPreview::BuildLines(const SceneObject &object, Lines &lines) const {
	if (!object.respawn || !object.respawn->IsValid()) {
		return false;
	}

	const auto &settings = *object.respawn;
	const auto position = settings.useStartPosition
	                          ? object.transform.translation
	                          : settings.position;

	if (!MathUtils::IsFinite(position)) {
		return false;
	}

	const DirectX::XMFLOAT3 below{position.x, settings.minimumY, position.z};
	const std::array<DirectX::XMFLOAT3, 4> corners{{
	    {position.x - kFallPlaneHalfExtent, settings.minimumY,
	     position.z - kFallPlaneHalfExtent},
	    {position.x + kFallPlaneHalfExtent, settings.minimumY,
	     position.z - kFallPlaneHalfExtent},
	    {position.x + kFallPlaneHalfExtent, settings.minimumY,
	     position.z + kFallPlaneHalfExtent},
	    {position.x - kFallPlaneHalfExtent, settings.minimumY,
	     position.z + kFallPlaneHalfExtent},
	}};
	Lines result{};
	result[0] = {{position.x - kMarkerRadius, position.y, position.z},
	             {position.x + kMarkerRadius, position.y, position.z},
	             positionColor_};
	result[1] = {{position.x, position.y - kMarkerRadius, position.z},
	             {position.x, position.y + kMarkerRadius, position.z},
	             positionColor_};
	result[2] = {{position.x, position.y, position.z - kMarkerRadius},
	             {position.x, position.y, position.z + kMarkerRadius},
	             positionColor_};
	result[3] = {position, below, positionColor_};

	for (std::size_t i = 0; i < corners.size(); ++i) {
		result[4 + i] = {corners[i], corners[(i + 1) % corners.size()],
		                 fallColor_};
	}

	result[8] = {corners[0], corners[2], fallColor_};
	result[9] = {corners[1], corners[3], fallColor_};
	lines = result;
	return true;
}

bool RespawnPreview::Update(SceneRenderer &renderer,
                            const SceneObject &object) const {
	Lines lines;

	if (!BuildLines(object, lines)) {
		return true;
	}

	for (const auto &line : lines) {
		if (!renderer.AddDebugLine(line)) {
			return false;
		}
	}

	return true;
}
