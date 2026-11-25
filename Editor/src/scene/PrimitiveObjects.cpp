#include "scene/PrimitiveObjects.h"

SceneObject PrimitiveObjects::CreateBox() {
	SceneObject object;
	object.name = "Box";
	object.mesh = MeshComponent{L"resources/editor/models/box.glb"};
	object.meshRenderer = MeshRendererComponent{};
	object.collider = Collider{{0, 0, 0}, {1, 1, 1}};
	return object;
}
