#include "BoxLesson.h"
#include "scene/Scene.h"
#include "scene/SceneLighting.h"

SceneObjectId AddLessonCube(Scene &scene, const char *name,
                            const DirectX::XMFLOAT3 &position,
                            const DirectX::XMFLOAT3 &size,
                            const DirectX::XMFLOAT3 &color) {
	constexpr auto model = L"resources/tutorial/models/Box.obj";
	const auto id = scene.CreateObject(name, model);
	auto &object = *scene.FindObject(id);
	object.transform.translation = position;
	object.transform.scale = size;
	object.colorTint = ColorTint{color};
	return id;
}

void AddLessonLight(Scene &scene) {
	LightingSettings light;
	light.direction = {0.7f, -1.8f, 0.6f};
	light.shadows.enabled = true;
	light.shadows.resolution = 512;
	light.shadows.extent = 10;
	scene.CreateObject(SceneLighting::CreateDirectionalLight(light));
}

SceneObjectId CreateLessonBox(Scene &scene) {
	// 第2章の課題。位置と大きさを変更してビルドする。
	Transform transform;
	transform.translation = {0.f, 0.65f, 0.f};
	transform.scale = {1.3f, 1.3f, 1.3f};
	const auto box = AddLessonCube(scene, "緑の箱", transform.translation,
	                               transform.scale, {0.21f, 0.80f, 0.62f});
	const auto marker = AddLessonCube(scene, "子オブジェクト", {0.52f, 0.5f, -0.09f},
	                                  {0.30f, 0.30f, 0.30f}, {1.f, 0.70f, 0.23f});
	scene.FindObject(marker)->parentId = box;
	AddLessonCube(scene, "床", {0, -0.08f, 0}, {9, 0.16f, 9}, {0.32f, 0.40f, 0.47f});
	AddLessonLight(scene);
	return box;
}
