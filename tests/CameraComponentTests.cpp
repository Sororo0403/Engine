#include "gameplay/SampleStage.h"
#include "gameplay/SceneSimulation.h"
#include "graphics/camera/Camera.h"
#include "history/EditorHistory.h"
#include "math/Ray.h"
#include "math/TransformUtils.h"
#include "platform/windows/PathUtils.h"
#include "scene/SceneSerializer.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <json.hpp>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool value, const char *message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}
bool Near(float a, float b) { return std::abs(a - b) < 0.003f; }
void Projection() {
	Camera camera;
	CameraSettings lens{90, 0.2f, 500};
	Require(camera.SetSettings(lens) && camera.Update(800, 800),
	        "camera projection settings");
	const auto projection = camera.GetProjectionMatrix();
	Require(Near(projection._22, 1), "vertical FOV not applied");
	Ray ray;
	Require(camera.CreatePickingRay(0.5f, 0.5f, ray) &&
	            Near(ray.origin.z, -1.8f),
	        "near clip not applied");
	for (const auto bad :
	     {CameraSettings{0, .1f, 100}, CameraSettings{180, .1f, 100},
	      CameraSettings{45, 0, 100}, CameraSettings{45, 1, 1},
	      CameraSettings{45, 2, 1},
	      CameraSettings{45, .1f, std::numeric_limits<float>::infinity()}}) {
		Require(!camera.SetSettings(bad) && camera.Update(800, 800) &&
		            camera.GetProjectionMatrix()._22 == projection._22 &&
		            camera.GetProjectionMatrix()._33 == projection._33,
		        "invalid lens changed projection");
	}
}
void Simulation() {
	Scene scene;
	SceneObject camera;
	camera.name = "Fixed";
	camera.camera = CameraComponent{};
	camera.camera->settings = {60, .2f, 300};
	camera.transform.translation = {3, 2, -7};
	camera.transform.rotation =
	    TransformUtils::RotationFromEuler({0, DirectX::XM_PIDIV2, 0});
	const auto id = scene.CreateObject(camera);
	SceneSimulation play;
	std::string error;
	Require(play.Start(scene, error) &&
	            play.GetCameraSettings() == camera.camera->settings,
	        "fixed camera start");
	const auto pose = play.GetCameraPose();
	Require(Near(pose.position.x, 3) && Near(pose.target.x, 4) &&
	            Near(pose.target.z, -7),
	        "fixed camera Transform rotation ignored");
	for (int i = 0; i < 30; ++i) {
		Require(play.Update(1.f / 120, {}), "fixed camera update");
	}
	Require(Near(play.GetCameraPose().position.z, -7), "fixed camera moved");
	const auto other = scene.CreateObject(camera);
	Require(!play.Start(scene, error) && !error.empty() && play.IsPlaying(),
	        "duplicate Primary accepted");
	scene.FindObject(other)->camera->primary = false;
	Require(play.Start(scene, error), "inactive camera incorrectly rejected");
	scene.FindObject(id)->camera->primary = false;
	CameraPose fallback;
	fallback.position = {0, 4, -10};
	fallback.target = {0, 1, 0};
	Require(play.Start(scene, error, fallback) &&
	            Near(play.GetCameraPose().position.y, 4) &&
	            play.GetCameraSettings() == CameraSettings{},
	        "no Primary fallback failed");
	scene.ReplaceObjects(SampleStage::Create());
	scene.FindObject(6)->camera->primary = false;
	SceneObject fixed;
	fixed.camera = CameraComponent{};
	fixed.transform.translation = {10, 5, -10};
	const auto primary = scene.CreateObject(fixed);
	Require(play.Start(scene, error), "independent fixed/follow camera start");
	for (int i = 0; i < 120; ++i) {
		Require(play.Update(1.f / 120, {1, 0, false}),
		        "inactive follow behavior");
	}
	Require(
	    play.GetScene().FindObject(6)->transform.translation.x > 4 &&
	        Near(play.GetCameraPose().position.x, 10) &&
	        Near(play.GetScene().FindObject(primary)->transform.translation.x,
	             10),
	    "FollowCamera did not update its Transform independently of Primary "
	    "selection");
	scene.FindObject(6)->camera.reset();
	Require(!play.Start(scene, error), "FollowCamera missing Camera accepted");
	scene.ReplaceObjects(SampleStage::Create());
	Require(play.Start(scene, error), "sample camera start");
	for (int i = 0; i < 60; ++i) {
		Require(play.Update(1.f / 120, {1, 0, false}), "sample follow update");
	}
	const auto &runtime = play.GetScene().FindObject(6)->transform;
	Require(Near(runtime.translation.x, play.GetCameraPose().position.x) &&
	            runtime.translation.x > 1 && TransformUtils::IsValid(runtime) &&
	            scene.FindObject(6)->transform.translation.x == 0,
	        "follow/render Transform or editing isolation failed");
}
void HistoryAndPersistence() {
	Scene scene;
	const auto id = scene.CreateObject("Camera");
	EditorHistory history(scene);
	Require(history.SetCamera(id, CameraComponent{}) && history.Undo() &&
	            !scene.FindObject(id)->camera && history.Redo(),
	        "Camera add Undo");
	history.MarkSaved();
	const auto states = history.GetStateCount();
	Require(history.BeginCamera(id), "begin lens drag");
	scene.FindObject(id)->camera->settings.verticalFovDegrees = 70;
	history.CommitEdit();
	Require(history.GetStateCount() == states + 1 && history.Undo() &&
	            !history.IsModified() &&
	            scene.FindObject(id)->camera->settings.verticalFovDegrees ==
	                45 &&
	            history.Redo(),
	        "lens drag grouped Undo");
	const auto duplicate = history.DuplicateObject(id);
	Require(duplicate &&
	            scene.FindObject(duplicate)->camera ==
	                scene.FindObject(id)->camera &&
	            history.DeleteObject(duplicate) && history.Undo() &&
	            scene.FindObject(duplicate)->camera,
	        "Camera duplicate/delete Undo");
	Require(history.SetCamera(id, std::nullopt) && history.Undo() &&
	            scene.FindObject(id)->camera,
	        "Camera remove Undo");
	auto invalid = *scene.FindObject(id)->camera;
	invalid.settings.farClip = 0;
	Require(!history.SetCamera(id, invalid), "invalid Camera history accepted");
	scene.ReplaceObjects(SampleStage::Create());
	scene.FindObject(6)->camera->settings = {80, .25f, 500};
	const auto path =
	    PathUtils::GetExecutableDirectory() /
	    ("camera-test-" +
	     std::to_string(
	         std::chrono::steady_clock::now().time_since_epoch().count()) +
	     ".json");
	Require(SceneSerializer::Save(scene, path), "Camera scene save");
	Scene loaded;
	Require(SceneSerializer::Load(loaded, path) &&
	            loaded.FindObject(6)->camera == scene.FindObject(6)->camera,
	        "Camera persistence");
	nlohmann::json data;
	{
		std::ifstream file(path);
		file >> data;
	}
	Require(data["version"] == 6, "Camera scene version");
	data["version"] = 5;
	for (auto &object : data["objects"]) {
		object.erase("camera");
	}
	{
		std::ofstream file(path);
		file << data;
	}
	Require(SceneSerializer::Load(loaded, path) &&
	            loaded.FindObject(6)->camera &&
	            loaded.FindObject(6)->camera->primary &&
	            loaded.FindObject(6)->followCamera,
	        "v5 FollowCamera migration");
	SceneSimulation play;
	std::string error;
	Require(play.Start(loaded, error), "migrated scene cannot Play");
	data["version"] = 6;
	data["objects"][5]["camera"] = {{"primary", true},
	                                {"verticalFovDegrees", false},
	                                {"nearClip", .1f},
	                                {"farClip", 100}};
	{
		std::ofstream file(path);
		file << data;
	}
	Require(!SceneSerializer::Load(loaded, path) &&
	            loaded.FindObject(6)->camera->IsValid(),
	        "invalid Camera load replaced scene");
	std::filesystem::remove(path);
}
} // namespace
void RunCameraComponentTests() {
	Projection();
	Simulation();
	HistoryAndPersistence();
	std::cout << "PASS: fixed Camera Transform/projection, Primary selection, "
	             "independent FollowCamera, Undo, v6 persistence/v5 migration "
	             "and invalid-state preservation\n";
}
