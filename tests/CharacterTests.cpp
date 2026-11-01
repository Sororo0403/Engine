#include "gameplay/CharacterController.h"
#include "math/TransformUtils.h"
#include "physics/BoxColliderUtils.h"
#include "physics/CollisionWorld.h"
#include "scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool condition, const char *message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

SceneObjectId AddBox(Scene &scene, DirectX::XMFLOAT3 center,
                     DirectX::XMFLOAT3 halfExtents) {
	const auto id = scene.CreateObject();
	const auto object = scene.FindObject(id);
	object->transform.translation = center;
	object->collider = BoxCollider{{}, halfExtents};
	return id;
}

void CollisionTests() {
	Scene scene;
	const auto wall = AddBox(scene, {2, 0, 0}, {0.001f, 10, 1000});
	scene.FindObject(wall)->visible = false;
	CollisionWorld world;
	Require(world.Rebuild(scene), "collision world build");
	const AxisAlignedBox body{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
	CollisionMoveResult result;
	Require(world.Move(body, {100, 0, 0}, result) && result.hitWall &&
	            result.displacement.x > 1.498f &&
	            result.displacement.x < 1.499f,
	        "swept collision tunneled through thin hidden wall");
	Require(world.Move(body, {100, 0, 100}, result) && result.hitWall &&
	            result.displacement.x < 1.499f &&
	            std::abs(result.displacement.z - 100) < 0.001f,
	        "wall collision lost tangential motion");
	const AxisAlignedBox rightBody{{3.5f, -0.5f, -0.5f}, {4.5f, 0.5f, 0.5f}};
	Require(world.Move(rightBody, {-100, 0, 0}, result) && result.hitWall &&
	            result.displacement.x < -1.498f &&
	            result.displacement.x > -1.499f,
	        "negative swept collision tunneled");
	const auto invalid = AddBox(scene, {0, 0, 0}, {-1, 1, 1});
	Require(!world.Rebuild(scene) && world.Move(body, {100, 0, 0}, result) &&
	            result.hitWall,
	        "failed rebuild replaced collision data");
	scene.DestroyObject(invalid);
	Require(world.Rebuild(scene, wall) &&
	            world.Move(body, {100, 0, 0}, result) && !result.hitWall &&
	            result.displacement.x == 100,
	        "excluded actor collided with itself");

	Scene room;
	AddBox(room, {0, -0.5f, 0}, {100, 0.5f, 100});
	AddBox(room, {0, 2, 0}, {100, 0.25f, 100});
	AddBox(room, {2, 0.5f, 0}, {0.25f, 1, 100});
	AddBox(room, {0, 0.5f, 2}, {100, 1, 0.25f});
	Require(world.Rebuild(room), "room collision build");
	const AxisAlignedBox standing{{-0.5f, 0, -0.5f}, {0.5f, 1, 0.5f}};
	Require(world.CanPlace(standing) &&
	            world.Move(standing, {10, -100, 10}, result) &&
	            result.grounded && result.hitWall &&
	            result.displacement.x < 1.25f &&
	            result.displacement.z < 1.25f && result.displacement.y == 0,
	        "simultaneous floor and corner collision");
	Require(world.Move(standing, {0, 100, 0}, result) && result.hitCeiling &&
	            !result.grounded && result.displacement.y > 0.749f &&
	            result.displacement.y < 0.75f,
	        "ceiling sweep failed");
	CollisionMoveResult sentinel{{9, 8, 7}, true, true, true};
	result = sentinel;
	Require(!world.Move({{-1, -1, -1}, {1, 1, 1}}, {}, result) &&
	            result.displacement.x == 9 &&
	            !world.Move(standing,
	                        {std::numeric_limits<float>::quiet_NaN(), 0, 0},
	                        result) &&
	            result.displacement.y == 8,
	        "failed move changed result");

	Scene corner;
	AddBox(corner, {2, 0, 0}, {0.5f, 0.5f, 0.5f});
	Require(world.Rebuild(corner) && world.Move(body, {3, 3, 0}, result) &&
	            !result.hitWall && result.displacement.x == 3 &&
	            result.displacement.y == 3,
	        "grazing a corner stopped nonpenetrating motion");
	std::cout << "PASS: continuous thin-wall sweeps, hidden solids, both "
	             "directions, wall sliding, corner/floor/ceiling contacts, "
	             "grazing and atomic failures\n";
}

void CharacterTests() {
	Scene scene;
	AddBox(scene, {0, -0.5f, 0}, {100, 0.5f, 100});
	CollisionWorld world;
	Require(world.Rebuild(scene), "character world build");
	Transform spawn;
	spawn.translation = {0, 0.5f, 0};
	BoxCollider collider;
	CharacterSettings settings;
	CharacterController player;
	Require(!player.Update(0.01f, {}, world) &&
	            player.Initialize(spawn, collider, settings, world) &&
	            player.IsGrounded(),
	        "initial ground contact");
	float distances[3]{};
	int index = 0;
	for (int fps : {30, 60, 120}) {
		Require(player.Initialize(spawn, collider, settings, world),
		        "rate init");
		for (int frame = 0; frame < fps * 2; ++frame) {
			Require(player.Update(1.0f / static_cast<float>(fps), {1, 0, false},
			                      world),
			        "rate movement");
		}
		distances[index++] = player.GetTransform().translation.x;
		Require(std::abs(player.GetTransform().translation.x - 10.0f) <
		                0.001f &&
		            player.IsGrounded(),
		        "speed or grounded state depends on frame rate");
	}
	Require(std::abs(distances[0] - distances[2]) < 0.0001f,
	        "fixed-step rates differ");
	Require(player.Initialize(spawn, collider, settings, world),
	        "diagonal init");
	for (int frame = 0; frame < 120; ++frame) {
		Require(player.Update(1.0f / 120, {1, 1, false}, world),
		        "diagonal update");
	}
	const auto diagonal = player.GetTransform().translation;
	Require(std::abs(std::hypot(diagonal.x, diagonal.z) - 5.0f) < 0.001f,
	        "diagonal speed boost");

	CharacterController reference;
	Require(player.Initialize(spawn, collider, settings, world) &&
	            reference.Initialize(spawn, collider, settings, world),
	        "jump init");
	float peak = spawn.translation.y;
	for (int frame = 0; frame < 180; ++frame) {
		Require(player.Update(1.0f / 120, {0, 0, frame == 0 || frame == 20},
		                      world) &&
		            reference.Update(1.0f / 120, {0, 0, frame == 0}, world),
		        "jump update");
		peak = (std::max)(peak, player.GetTransform().translation.y);
		Require(std::abs(player.GetTransform().translation.y -
		                 reference.GetTransform().translation.y) < 0.00001f,
		        "midair press allowed a second jump");
	}
	Require(peak > 1.65f && peak < 1.75f && player.IsGrounded() &&
	            player.GetVerticalVelocity() == 0 &&
	            player.GetTransform().translation.y >= 0.5f &&
	            player.GetTransform().translation.y < 0.501f,
	        "jump arc or landing");
	Require(player.Initialize(spawn, collider, settings, world) &&
	            player.Update(0.001f, {0, 0, true}, world) &&
	            player.Update(0.008f, {}, world) &&
	            player.GetVerticalVelocity() > 0,
	        "short-frame jump press was lost");
	Require(player.Teleport(spawn.translation, world) && player.IsGrounded() &&
	            player.GetVerticalVelocity() == 0,
	        "teleport did not reset movement state");
	Require(!player.Teleport({0, -0.5f, 0}, world) &&
	            player.GetTransform().translation.y == 0.5f,
	        "failed teleport changed position");
	auto badSettings = settings;
	badSettings.gravity = 0;
	Require(!player.Initialize(spawn, collider, badSettings, world) &&
	            !player.Update(-1, {}, world) &&
	            !player.Update(
	                0.01f, {std::numeric_limits<float>::infinity(), 0, false},
	                world) &&
	            player.GetTransform().translation.y == 0.5f,
	        "invalid character state changed position");
	Require(player.Update(10, {1, 0, false}, world) &&
	            player.GetTransform().translation.x <= 0.501f,
	        "pause-sized interval teleported player");

	AddBox(scene, {3, 1, 0}, {0.25f, 1, 100});
	AddBox(scene, {0, 2, 0}, {100, 0.25f, 100});
	Require(world.Rebuild(scene) &&
	            player.Initialize(spawn, collider, settings, world),
	        "ceiling init");
	bool bumped = false;
	for (int frame = 0; frame < 240; ++frame) {
		Require(player.Update(1.0f / 120, {1, 0, frame == 0}, world),
		        "wall/ceiling update");
		Require(player.GetTransform().translation.y <= 1.2501f &&
		            player.GetTransform().translation.x < 2.25f,
		        "character crossed ceiling/wall");
		bumped |= player.GetTransform().translation.y > 1.24f &&
		          player.GetVerticalVelocity() == 0;
	}
	Require(bumped && player.IsGrounded(),
	        "ceiling collision did not stop ascent and allow landing");

	Scene ledge;
	AddBox(ledge, {0, -0.5f, 0}, {2, 0.5f, 2});
	Require(world.Rebuild(ledge) &&
	            player.Initialize(spawn, collider, settings, world),
	        "ledge init");
	for (int frame = 0; frame < 240; ++frame) {
		Require(player.Update(1.0f / 120, {1, 0, false}, world),
		        "ledge update");
	}
	Require(!player.IsGrounded() && player.GetTransform().translation.y < -10 &&
	            player.GetVerticalVelocity() >= -settings.terminalSpeed,
	        "ledge fall or terminal speed");
	Require(player.Teleport(spawn.translation, world) && player.IsGrounded(),
	        "return to spawn after fall");
	std::cout
	    << "PASS: fixed-step walking/jumping, 30/60/120Hz equivalence, "
	       "diagonal normalization, no air jump, latched short input, "
	       "landing/ceiling/wall/ledge, terminal speed and safe teleport\n";
}
void GroundPrecisionTests() {
	Scene scene;
	AddBox(scene, {0, -0.5f, 0}, {10000, 0.5f, 10000});
	CollisionWorld world;
	Require(world.Rebuild(scene), "precision world");
	for (int sample = 0; sample < 12; ++sample) {
		BoxCollider collider{{0.13f, 0.17f + static_cast<float>(sample) * 0.01f, 0.19f}, {0.37f, 0.73f, 0.41f}};
		Transform spawn;
		spawn.translation.y = 3;
		spawn.scale = {1.11f, 1.37f, 0.91f};
		spawn.rotation = TransformUtils::RotationFromEuler({0, 0.3f, 0});
		CharacterSettings settings;
		settings.moveSpeed = CharacterSettings::kMaximumSpeed;
		CharacterController player;
		Require(player.Initialize(spawn, collider, settings, world), "offset collider init");
		for (int frame = 0; frame < 480; ++frame) {
			Require(player.Update(1.0f / 120, {1, 0, false}, world), "offset scaled collider stuck while landing or walking");
		}
		Require(player.IsGrounded(), "offset scaled collider lost ground");
	}
	std::cout << "PASS: offset/scaled/rotated characters remain grounded while moving at maximum speed\n";
}
} // namespace

void RunCharacterTests() {
	CollisionTests();
	CharacterTests();
	GroundPrecisionTests();
}
