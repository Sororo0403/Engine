#include "gameplay/SampleStage.h"

std::map<SceneObjectId, SceneObject> SampleStage::Create() {
	std::map<SceneObjectId, SceneObject> objects;
	auto box = [&](SceneObjectId id, const char *name,
	               DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 scale) {
		SceneObject object;
		object.name = name;
		object.modelPath = L"resources/editor/models/box.glb";
		object.transform.translation = position;
		object.transform.scale = scale;
		object.collider = BoxCollider{{0, 0, 0}, {1, 1, 1}};
		objects.emplace(id, std::move(object));
	};
	box(1, "Floor", {0, -0.5f, 0}, {8, 0.5f, 8});
	box(2, "Left Wall", {-8, 1.5f, 0}, {0.5f, 2, 8});
	box(3, "Right Wall", {8, 1.5f, 0}, {0.5f, 2, 8});
	box(4, "Back Wall", {0, 1.5f, 8}, {8, 2, 0.5f});
	box(5, "Player", {0, 1.1f, -3}, {0.4f, 0.9f, 0.4f});
	objects.at(5).character = CharacterSettings{};
	objects.at(5).respawn = RespawnSettings{};
	SceneObject camera;
	camera.name = "Follow Camera";
	camera.followCamera = FollowCameraSettings{};
	camera.followCamera->target = 5;
	objects.emplace(6, std::move(camera));
	return objects;
}
