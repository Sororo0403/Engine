#include "selection/SelectionPivot.h"

#include "math/TransformUtils.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
#include "selection/EditorSelection.h"

bool SelectionPivot::Make(const Scene &scene, const EditorSelection &selection,
                          Mode mode, Transform &transform) {
	if (selection.GetCount() < 2 ||
	    (mode != Mode::CENTER && mode != Mode::ACTIVE)) {
		return false;
	}

	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
	DirectX::XMFLOAT3 activePosition{};
	bool activeFound = false;

	for (const auto id : selection.GetIds()) {
		SceneWorldTransform world;

		if (!SceneHierarchy::GetWorldTransform(scene, id, world)) {
			return false;
		}

		const auto position = world.GetPosition();
		x += position.x;
		y += position.y;
		z += position.z;

		if (id == selection.GetActiveId()) {
			activePosition = position;
			activeFound = true;
		}
	}

	if (!activeFound) {
		return false;
	}

	const double count = static_cast<double>(selection.GetCount());
	Transform result;
	result.translation = mode == Mode::ACTIVE
	                         ? activePosition
	                         : DirectX::XMFLOAT3{static_cast<float>(x / count),
	                                             static_cast<float>(y / count),
	                                             static_cast<float>(z / count)};

	if (!TransformUtils::IsValid(result)) {
		return false;
	}

	transform = result;
	return true;
}
