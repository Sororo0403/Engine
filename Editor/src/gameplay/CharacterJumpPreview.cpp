#include "gameplay/CharacterJumpPreview.h"

#include "gameplay/CharacterController.h"
#include "physics/CollisionWorld.h"
#include "scene/Scene.h"
#include "scene/SceneRenderer.h"

bool CharacterJumpPreview::BuildPaths(const Scene &scene, SceneObjectId id,
                                      Paths &paths) const {
	const auto *object = scene.FindObject(id);

	if (!object || !object->character || !object->collider ||
	    (object->respawn &&
	     (!object->respawn->IsValid() ||
	      object->transform.translation.y <= object->respawn->minimumY))) {
		return false;
	}

	CollisionWorld world;
	CharacterController initial;

	if (!world.Rebuild(scene, id) ||
	    !initial.Initialize(object->transform, *object->collider,
	                        *object->character, world) ||
	    !initial.IsGrounded()) {
		return false;
	}

	const std::array<CharacterInput, kDirectionCount> directions{
	    {{0, 0, true},
	     {1, 0, true},
	     {-1, 0, true},
	     {0, 1, true},
	     {0, -1, true}}};
	Paths result;

	for (std::size_t i = 0; i < directions.size(); ++i) {
		auto controller = initial;
		auto input = directions[i];
		auto &path = result[i];
		path.points[path.count++] = object->transform.translation;

		for (std::size_t step = 1; step <= kMaximumSteps; ++step) {
			if (!controller.Update(kStepSeconds, input, world)) {
				return false;
			}

			input.jumpPressed = false;
			const auto position = controller.GetTransform().translation;
			const bool fell =
			    object->respawn && position.y < object->respawn->minimumY;
			const bool landed = controller.IsGrounded();
			const bool finished = fell || landed || step == kMaximumSteps;

			if (step % kSampleInterval == 0 || finished) {
				path.points[path.count++] = position;
			}

			if (finished) {
				path.end = fell     ? End::FALL_LIMIT
				           : landed ? End::LANDED
				                    : End::TIME_LIMIT;
				break;
			}
		}
	}

	paths = result;
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
	     {.4f, .7f, 1, 1}}};

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
