#include "BoxLesson.h"
#include "scene/Scene.h"
#include "scene/SceneLighting.h"

SceneObjectId CreateLessonBox(Scene &scene) {
	constexpr auto model = L"resources/tutorial/models/Box.obj";

	// 第2章の課題。位置と大きさを変更してビルドする。
	const auto box = scene.CreateObject("緑の箱", model);
	auto &parent = *scene.FindObject(box);
	parent.transform.translation = {0.0f, 0.65f, 0.0f};
	parent.transform.scale = {1.3f, 1.3f, 1.3f};
	parent.colorTint = ColorTint{{0.21f, 0.80f, 0.62f}};

	const auto marker = scene.CreateObject("子オブジェクト", model);
	auto &child = *scene.FindObject(marker);
	child.parentId = box;
	child.transform.translation = {0.52f, 0.5f, -0.09f};
	child.transform.scale = {0.30f, 0.30f, 0.30f};
	child.colorTint = ColorTint{{1.0f, 0.70f, 0.23f}};

	const auto floor = scene.CreateObject("床", model);
	auto &surface = *scene.FindObject(floor);
	surface.transform.translation = {0, -0.08f, 0};
	surface.transform.scale = {9, 0.16f, 9};
	surface.colorTint = ColorTint{{0.32f, 0.40f, 0.47f}};

	LightingSettings light;
	light.direction = {0.7f, -1.8f, 0.6f};
	light.shadows.enabled = true;
	light.shadows.resolution = 512;
	light.shadows.extent = 10;
	scene.CreateObject(SceneLighting::CreateDirectionalLight(light));
	return box;
}
