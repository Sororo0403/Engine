#include "scene/PrimitiveObjects.h"

SceneObject PrimitiveObjects::CreateBox() {
	SceneObject object;
	object.name = "Box";
	object.modelPath = L"resources/editor/models/box.glb";
	object.collider = BoxCollider{{0, 0, 0}, {1, 1, 1}};
	return object;
}
