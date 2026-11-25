#include "gameplay/CharacterJumpPreview.h"
#include "scene/SceneActivity.h"

#include "gameplay/CharacterController.h"
#include "math/MathUtils.h"
#include "physics/CollisionWorld.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneRenderer.h"
#include <algorithm>
#include <cmath>

bool CharacterJumpPreview::BuildPaths(const Scene &scene, SceneObjectId id,
                                      Paths &paths) const {
	const auto *object = scene.FindObject(id);
	SceneWorldTransform transform;

	if (!object || !SceneActivity::IsActive(scene, id) ||
	    !SceneHierarchy::GetWorldTransform(scene, id, transform) ||
	    !object->character || !object->collider ||
	    (object->respawn &&
	     (!object->respawn->IsValid() ||
	      !object->respawn->IsSafe(transform.GetPosition())))) {
		return false;
	}

	CollisionWorld world;
	CharacterController initial;

	std::vector<SceneObjectId> ignored;
	if (!SceneHierarchy::GetSubtree(scene, id, ignored) ||
	    !world.Rebuild(scene, std::span<const SceneObjectId>{ignored}) ||
	    !initial.Initialize(transform.matrix, *object->collider,
	                        *object->character, world) ||
	    !initial.IsGrounded()) {
		return false;
	}

	const std::array<CharacterInput, kDirectionCount> directions{
	    {{0, 0, true},
	     {1, 0, true},
	     {-1, 0, true},
	     {0, 1, true},
	     {0, -1, true},
	     {1, 1, true},
	     {1, -1, true},
	     {-1, 1, true},
	     {-1, -1, true}}};
	Paths result;

	const auto origin = transform.GetPosition();
	const auto up = object->character->GetUp();
	for (std::size_t i = 0; i < directions.size(); ++i) {
		auto controller = initial;
		auto input = directions[i];
		auto &path = result[i];
		path.points[path.count++] = origin;

		for (std::size_t step = 1; step <= kMaximumSteps; ++step) {
			if (!controller.Update(kStepSeconds, input, world)) {
				return false;
			}

			input.jumpPressed = false;
			const auto position = controller.GetTransform().translation;
			path.durationSeconds = static_cast<double>(step) * kStepSeconds;
			const double x = double(position.x) - origin.x;
			const double y = double(position.y) - origin.y;
			const double z = double(position.z) - origin.z;
			const double height = x * up.x + y * up.y + z * up.z;
			path.maximumHeight = (std::max)(path.maximumHeight, height);
			path.horizontalDistance = std::hypot(x - height * up.x,
			                                     y - height * up.y, z - height * up.z);
			const bool fell =
			    object->respawn && object->respawn->HasCrossed(position);
			const bool landed = controller.IsGrounded();
			const bool finished = fell || landed || step == kMaximumSteps;

			if (step % kSampleInterval == 0 || finished) {
				path.points[path.count++] = position;
			}

			if (finished) {
				path.end = fell     ? End::FALL_LIMIT
				           : landed ? End::LANDED
				                    : End::TIME_LIMIT;

				if (path.end == End::LANDED &&
				    !controller.FindGroundObject(world, path.landingObject)) {
					return false;
				}

				break;
			}
		}
	}

	paths = result;
	return true;
}

bool CharacterJumpPreview::GetFrameBounds(
    const Scene &scene, SceneObjectId id,
    std::array<DirectX::XMFLOAT3, 8> &corners) const {
	Paths paths;

	if (!BuildPaths(scene, id, paths)) {
		return false;
	}

	auto minimum = paths[0].points[0];
	auto maximum = minimum;

	for (const auto &path : paths) {
		for (std::size_t i = 0; i < path.count; ++i) {
			const auto point = path.points[i];
			minimum.x = (std::min)(minimum.x, point.x - kMarkerRadius);
			minimum.y = (std::min)(minimum.y, point.y - kMarkerRadius);
			minimum.z = (std::min)(minimum.z, point.z - kMarkerRadius);
			maximum.x = (std::max)(maximum.x, point.x + kMarkerRadius);
			maximum.y = (std::max)(maximum.y, point.y + kMarkerRadius);
			maximum.z = (std::max)(maximum.z, point.z + kMarkerRadius);
		}
	}

	if (!MathUtils::IsFinite(minimum) || !MathUtils::IsFinite(maximum)) {
		return false;
	}

	std::array<DirectX::XMFLOAT3, 8> result;

	for (std::size_t i = 0; i < result.size(); ++i) {
		result[i] = {(i & 1) ? maximum.x : minimum.x,
		             (i & 2) ? maximum.y : minimum.y,
		             (i & 4) ? maximum.z : minimum.z};
	}

	corners = result;
	return true;
}

bool CharacterJumpPreview::Update(SceneRenderer &renderer, const Scene &scene,
                                  SceneObjectId id) const {
	Paths paths;

	if (!BuildPaths(scene, id, paths)) {
		return true;
	}

	const std::array<DirectX::XMFLOAT4, kDirectionCount> colors{
	    {{1, 1, 1, 1},
	     {1, .45f, .4f, 1},
	     {1, .45f, .4f, 1},
	     {.4f, .7f, 1, 1},
	     {.4f, .7f, 1, 1},
	     {.8f, .5f, 1, 1},
	     {.8f, .5f, 1, 1},
	     {.8f, .5f, 1, 1},
	     {.8f, .5f, 1, 1}}};

	for (std::size_t i = 0; i < paths.size(); ++i) {
		const auto &path = paths[i];

		for (std::size_t p = 1; p < path.count; ++p) {
			if (!renderer.AddDebugLine(
			        {path.points[p - 1], path.points[p], colors[i]})) {
				return false;
			}
		}

		const auto end = path.points[path.count - 1];
		const DirectX::XMFLOAT4 color =
		    path.end == End::LANDED       ? DirectX::XMFLOAT4{.3f, 1, .4f, 1}
		    : path.end == End::FALL_LIMIT ? DirectX::XMFLOAT4{1, .2f, .2f, 1}
		                                  : DirectX::XMFLOAT4{1, .85f, .2f, 1};
		const std::array<DebugLine, 3> marker{
		    {{{end.x - kMarkerRadius, end.y, end.z},
		      {end.x + kMarkerRadius, end.y, end.z},
		      color},
		     {{end.x, end.y - kMarkerRadius, end.z},
		      {end.x, end.y + kMarkerRadius, end.z},
		      color},
		     {{end.x, end.y, end.z - kMarkerRadius},
		      {end.x, end.y, end.z + kMarkerRadius},
		      color}}};

		for (const auto &line : marker) {
			if (!renderer.AddDebugLine(line)) {
				return false;
			}
		}
	}

	return true;
}
