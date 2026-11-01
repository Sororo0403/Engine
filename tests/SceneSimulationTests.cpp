#include "gameplay/GameplayInput.h"
#include "gameplay/SampleStage.h"
#include "gameplay/SceneSimulation.h"
#include "history/EditorHistory.h"
#include "math/TransformUtils.h"
#include "platform/windows/PathUtils.h"
#include "scene/SceneDocument.h"
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
bool Near(float a, float b, float epsilon = 0.002f) {
	return std::abs(a - b) < epsilon;
}
void InputRouting() {
	Keyboard keys;
	Keyboard::State state{};
	state[static_cast<size_t>(Key::D)] = true;
	state[static_cast<size_t>(Key::W)] = true;
	state[static_cast<size_t>(Key::SPACE)] = true;
	keys.Update(state);
	ViewportInput view;
	view.windowFocused = true;
	view.applicationFocused = true;
	auto input = GameplayInput::Read(keys, view, false);
	Require(input.moveX == 1 && input.moveZ == 1 && input.jumpPressed,
	        "Game keyboard routing");
	keys.Update(state);
	Require(!GameplayInput::Read(keys, view, false).jumpPressed,
	        "held jump repeated");
	auto blocked = [&] {
		const auto input = GameplayInput::Read(keys, view, false);
		Require(input.moveX == 0 && input.moveZ == 0 && !input.jumpPressed,
		        "unfocused input leaked");
	};
	view.windowFocused = false;
	blocked();
	view.windowFocused = true;
	view.applicationFocused = false;
	blocked();
	view.applicationFocused = true;
	view.widgetActive = true;
	blocked();
	view.widgetActive = false;
	input = GameplayInput::Read(keys, view, true);
	Require(input.moveX == 0 && input.moveZ == 0 && !input.jumpPressed,
	        "text editing moved character");
}
void Components() {
	Scene scene;
	const auto id = scene.CreateObject("Actor");
	EditorHistory history(scene);
	Require(history.SetCharacter(id, CharacterSettings{}) &&
	            history.SetFollowCamera(id, FollowCameraSettings{}) &&
	            history.SetRespawn(id, RespawnSettings{}),
	        "component add");
	history.MarkSaved();
	const auto count = history.GetStateCount();
	Require(history.BeginCharacter(id), "begin character");
	scene.FindObject(id)->character->moveSpeed = 9;
	Require(history.IsModified(), "pending character dirty");
	history.CommitEdit();
	Require(history.GetStateCount() == count + 1 && history.Undo() &&
	            !history.IsModified() &&
	            scene.FindObject(id)->character->moveSpeed == 5 &&
	            history.Redo(),
	        "character grouped Undo");
	auto follow = *scene.FindObject(id)->followCamera;
	follow.smoothing = 2;
	Require(history.BeginFollowCamera(id), "begin camera");
	scene.FindObject(id)->followCamera = follow;
	history.CommitEdit();
	Require(history.Undo() &&
	            scene.FindObject(id)->followCamera->smoothing == 8 &&
	            history.Redo(),
	        "camera Undo");
	Require(history.BeginRespawn(id), "begin respawn");
	scene.FindObject(id)->respawn->minimumY = -25;
	history.CommitEdit();
	Require(history.Undo() && scene.FindObject(id)->respawn->minimumY == -10 &&
	            history.Redo(),
	        "respawn Undo");
	const auto states = history.GetStateCount();
	auto bad = *scene.FindObject(id)->character;
	bad.gravity = 0;
	Require(!history.SetCharacter(id, bad) && history.GetStateCount() == states,
	        "invalid character accepted");
	Require(history.SetRespawn(id, std::nullopt) &&
	            !scene.FindObject(id)->respawn && history.Undo() &&
	            scene.FindObject(id)->respawn->minimumY == -25,
	        "remove Undo");
	const auto duplicate = history.DuplicateObject(id);
	Require(duplicate &&
	            scene.FindObject(duplicate)->character ==
	                scene.FindObject(id)->character &&
	            scene.FindObject(duplicate)->followCamera ==
	                scene.FindObject(id)->followCamera &&
	            scene.FindObject(duplicate)->respawn ==
	                scene.FindObject(id)->respawn,
	        "duplicate components");
	Require(history.DeleteObject(id) && history.Undo() &&
	            scene.FindObject(id)->character->moveSpeed == 9,
	        "delete Undo components");
}
void OptionalComponents() {
	Scene empty;
	SceneSimulation play;
	std::string error;
	CameraPose fallback;
	fallback.position = {4, 6, -12};
	fallback.target = {1, 2, 0};
	auto samePose = [](const CameraPose &a, const CameraPose &b) {
		return a.position.x == b.position.x && a.position.y == b.position.y &&
		       a.position.z == b.position.z && a.target.x == b.target.x &&
		       a.target.y == b.target.y && a.target.z == b.target.z &&
		       a.up.x == b.up.x && a.up.y == b.up.y && a.up.z == b.up.z;
	};
	Require(play.Start(empty, error, fallback) && error.empty() &&
	            play.IsPlaying(),
	        "empty scene could not Play");
	Require(samePose(play.GetCameraPose(), fallback),
	        "empty scene ignored fallback camera");
	for (int i = 0; i < 120; ++i) {
		Require(play.Update(1.f / 120, {1, 1, true}),
		        "empty scene update failed");
	}
	Require(play.GetScene().GetObjects().empty() &&
	            samePose(play.GetCameraPose(), fallback),
	        "empty scene generated objects or changed camera");
	Require(!play.Update(-1, {}) && play.IsPlaying(),
	        "empty scene accepted invalid time");
	play.Stop();
	Require(play.Start(empty, error), "empty scene default camera failed");
	Require(samePose(play.GetCameraPose(), CameraPose{}),
	        "empty restart retained old camera");

	Scene visual;
	SceneObject object;
	object.name = "Display only";
	object.modelPath = "resources/editor/models/box.glb";
	object.transform.translation = {3, 4, 5};
	const auto target = visual.CreateObject(object);
	Require(play.Start(visual, error, fallback),
	        "model-only scene could not Play");
	for (int i = 0; i < 120; ++i) {
		Require(play.Update(1.f / 120, {1, 1, true}),
		        "model-only update failed");
	}
	Require(
	    TransformUtils::AreEqual(play.GetScene().FindObject(target)->transform,
	                             object.transform) &&
	        TransformUtils::AreEqual(visual.FindObject(target)->transform,
	                                 object.transform),
	    "model without behavior moved");

	SceneObject camera;
	camera.name = "Static follow";
	camera.camera = CameraComponent{};
	camera.followCamera = FollowCameraSettings{};
	camera.followCamera->target = target;
	const auto cameraId = visual.CreateObject(camera);
	auto invalid = fallback;
	invalid.position = invalid.target;
	Require(play.Start(visual, error, invalid),
	        "Follow Camera unnecessarily required character or valid unused "
	        "fallback");
	for (int i = 0; i < 120; ++i) {
		Require(play.Update(1.f / 120, {}), "camera-only update failed");
	}
	Require(Near(play.GetCameraPose().position.x, 3) &&
	            Near(play.GetCameraPose().position.y, 7) &&
	            Near(play.GetCameraPose().position.z, -2),
	        "camera-only pose wrong");
	visual.DestroyObject(cameraId);
	const auto cameraBefore = play.GetCameraPose();
	Require(!play.Start(visual, error, invalid) && !error.empty() &&
	            play.IsPlaying() &&
	            samePose(play.GetCameraPose(), cameraBefore),
	        "invalid fallback destroyed running session");

	Scene actor;
	auto objects = SampleStage::Create();
	objects.erase(6);
	objects.at(5).respawn.reset();
	actor.ReplaceObjects(std::move(objects));
	Require(play.Start(actor, error, fallback),
	        "character without camera/respawn could not Play");
	for (int i = 0; i < 120; ++i) {
		Require(play.Update(1.f / 120, {}), "character-only settling failed");
	}
	const auto ground = play.GetScene().FindObject(5)->transform.translation;
	Require(Near(ground.y, 0.9f), "character without camera did not land");
	Require(play.Update(1.f / 120, {1, 0, true}), "character-only jump failed");
	for (int i = 0; i < 24; ++i) {
		Require(play.Update(1.f / 120, {1, 0, false}),
		        "character-only movement failed");
	}
	const auto moved = play.GetScene().FindObject(5)->transform.translation;
	Require(moved.x > ground.x + 0.5f && moved.y > ground.y + 0.5f &&
	            samePose(play.GetCameraPose(), fallback),
	        "character-only movement changed fallback camera or failed");
	Require(
	    play.Start(empty, error, fallback) && play.Update(1.f / 120, {}),
	    "restart without former character/camera retained stale components");
	play.Stop();
	std::cout << "PASS: empty/model-only/character-only/camera-only Play, "
	             "fallback camera, restart and invalid-pose preservation\n";
}

void CharacterRequiresCollider() {
	Scene scene;
	scene.ReplaceObjects(SampleStage::Create());
	SceneSimulation play;
	std::string error;
	Require(play.Start(scene, error), "valid character startup failed");
	Require(play.Update(1.f / 120, {}), "valid character update failed");
	const auto before = play.GetScene().FindObject(5)->transform;
	scene.FindObject(5)->collider.reset();
	Require(!play.Start(scene, error) && !error.empty() &&
	            error.find("Box Collider") != std::string::npos,
	        "character without collider was accepted or error missing");
	Require(play.IsPlaying() &&
	            TransformUtils::AreEqual(
	                play.GetScene().FindObject(5)->transform, before),
	        "missing collider replaced active runtime");
	play.Stop();
	Require(!play.Start(scene, error) && !play.IsPlaying(),
	        "invalid character started stopped runtime");
	// The requirement belongs to CharacterController, not to Play itself.
	scene.FindObject(5)->character.reset();
	scene.FindObject(5)->respawn.reset();
	Require(play.Start(scene, error) && error.empty() &&
	            play.Update(1.f / 120, {}),
	        "scene without character incorrectly requires a collider");
	Require(!play.GetScene().FindObject(5)->collider,
	        "missing collider was silently created");
	play.Stop();
	Scene empty;
	Require(play.Start(empty, error) && error.empty() &&
	            play.Update(1.f / 120, {}),
	        "empty scene could not Play after validation failure");
	std::cout << "PASS: CharacterController requires Box Collider; missing "
	             "dependency preserves runtime; scenes without character still "
	             "Play\n";
}

void SimulationAndDocument() {
	SceneDocument document;
	document.GetScene().ReplaceObjects(SampleStage::Create());
	document.MarkModified();
	const auto directory =
	    PathUtils::GetExecutableDirectory() / L"scene-simulation-tests";
	const auto path =
	    directory /
	    (std::to_wstring(
	         std::chrono::steady_clock::now().time_since_epoch().count()) +
	     L".json");
	Require(document.SaveAs(path), "save authored template");
	SceneDocument loaded;
	Require(loaded.Open(path), "reload authored template");
	const auto original = loaded.GetScene().GetObjects();
	Require(original.at(5).character && original.at(5).respawn &&
	            original.at(6).followCamera &&
	            original.at(6).followCamera->target == 5,
	        "saved components lost");
	SceneSimulation play;
	std::string error;
	Require(play.Start(loaded.GetScene(), error) && error.empty(),
	        "sample Play failed");
	for (int i = 0; i < 120; ++i) {
		Require(play.Update(1.0f / 120, {}), "settle failed");
	}
	const float ground = play.GetScene().FindObject(5)->transform.translation.y;
	Require(Near(ground, 0.9f), "sample floor contact wrong");
	const auto before = play.GetCameraPose();
	CharacterInput jump;
	jump.jumpPressed = true;
	Require(play.Update(1.0f / 120, jump), "jump start");
	for (int i = 0; i < 24; ++i) {
		Require(play.Update(1.0f / 120, {}), "jump rise");
	}
	Require(play.GetScene().FindObject(5)->transform.translation.y >
	            ground + 0.5f,
	        "jump did not rise");
	Require(play.GetCameraPose().position.y > before.position.y,
	        "camera did not follow jump");
	for (int i = 0; i < 240; ++i) {
		Require(play.Update(1.0f / 120, {}), "landing");
	}
	Require(
	    Near(play.GetScene().FindObject(5)->transform.translation.y, ground),
	    "jump did not land");
	CharacterInput right;
	right.moveX = 1;
	for (int i = 0; i < 600; ++i) {
		Require(play.Update(1.0f / 120, right), "wall movement");
	}
	const auto atWall = play.GetScene().FindObject(5)->transform.translation;
	Require(atWall.x < 7.11f && atWall.x > 7.09f, "side wall tunnelling");
	Require(play.GetCameraPose().position.x > 7,
	        "camera did not follow movement");
	CharacterInput left;
	left.moveX = -1;
	for (int i = 0; i < 170; ++i) {
		Require(play.Update(1.0f / 120, left), "return from wall");
	}
	CharacterInput back;
	back.moveZ = -1;
	bool fell = false, respawned = false;
	for (int i = 0; i < 800; ++i) {
		Require(play.Update(1.0f / 120, back), "fall update");
		const auto p = play.GetScene().FindObject(5)->transform.translation;
		if (p.y < -5) {
			fell = true;
		}
		if (fell && Near(p.z, -3, 0.05f) && Near(p.y, 1.1f, 0.05f)) {
			respawned = true;
			Require(Near(play.GetCameraPose().position.y, p.y + 3, 0.01f) &&
			            Near(play.GetCameraPose().position.z, p.z - 7, 0.01f),
			        "camera failed to snap on respawn");
			break;
		}
	}
	Require(fell && respawned, "fall did not respawn");
	Require(
	    !loaded.IsModified() && loaded.GetHistory().GetStateCount() == 1 &&
	        TransformUtils::AreEqual(loaded.GetScene().FindObject(5)->transform,
	                                 original.at(5).transform),
	    "Play modified editing scene/history");
	// Invalid starts must preserve an already running session.
	Scene bad;
	bad.ReplaceObjects(original);
	bad.FindObject(5)->character->gravity = 0;
	const auto runtimeBefore = play.GetScene().FindObject(5)->transform;
	Require(!play.Start(bad, error) && !error.empty() && play.IsPlaying() &&
	            TransformUtils::AreEqual(
	                runtimeBefore, play.GetScene().FindObject(5)->transform),
	        "failed Start destroyed running scene");
	bad.ReplaceObjects(original);
	bad.FindObject(6)->followCamera->target = 999;
	Require(!play.Start(bad, error) && !error.empty(),
	        "missing camera target accepted");
	bad.ReplaceObjects(original);
	bad.FindObject(5)->transform.translation.y = -0.5f;
	Require(!play.Start(bad, error), "overlapping initial position accepted");
	bad.ReplaceObjects(original);
	bad.FindObject(5)->respawn->useStartPosition = false;
	bad.FindObject(5)->respawn->position = {0, -0.5f, 0};
	Require(!play.Start(bad, error), "blocked respawn accepted");
	const auto beforeInvalid = play.GetScene().FindObject(5)->transform;
	Require(!play.Update(std::numeric_limits<float>::quiet_NaN(), {}) &&
	            TransformUtils::AreEqual(
	                beforeInvalid, play.GetScene().FindObject(5)->transform),
	        "invalid update changed state");
	play.Stop();
	Require(!play.IsPlaying() && play.GetScene().GetObjects().empty() &&
	            !loaded.IsModified(),
	        "Stop did not discard runtime only");
	Require(
	    play.Start(loaded.GetScene(), error) &&
	        Near(play.GetScene().FindObject(5)->transform.translation.z, -3),
	    "second Play did not restore initial state");
	play.Stop();
	Require(
	    loaded.GetHistory().SetCharacter(5, CharacterSettings{8, 9, 25, 40}) &&
	        loaded.IsModified() && loaded.Save(),
	    "edited component save");
	SceneDocument again;
	Require(again.Open(path) &&
	            again.GetScene().FindObject(5)->character->moveSpeed == 8,
	        "edited component reload");
	// Invalid component input must preserve both a saved file and the editing document.
	nlohmann::json saved;
	{
		std::ifstream stream(path);
		stream >> saved;
	}
	auto invalid = saved;
	invalid["objects"][4]["character"]["gravity"] = false;
	{
		std::ofstream stream(path);
		stream << invalid;
	}
	const auto savedPath = again.GetFilePath();
	Require(!again.Open(path) && again.GetFilePath() == savedPath &&
	            again.GetScene().FindObject(5)->character->moveSpeed == 8,
	        "invalid component load damaged document");
	{
		std::ofstream stream(path);
		stream << saved;
	}
	again.GetScene().FindObject(5)->character->gravity = 0;
	Require(!again.Save(), "invalid component saved");
	nlohmann::json unchanged;
	{
		std::ifstream stream(path);
		stream >> unchanged;
	}
	Require(unchanged == saved, "invalid component save replaced valid file");
	std::filesystem::remove(path);
}
} // namespace
void RunSceneSimulationTests() {
	InputRouting();
	Components();
	OptionalComponents();
	CharacterRequiresCollider();
	SimulationAndDocument();
	std::cout << "PASS: component authoring/history, save/load, sample Play, "
	             "floor/wall/jump, camera, fall/respawn, Stop isolation and "
	             "validation\n";
}
