#include "gameplay/SceneValidation.h"
#include "animation/AnimationClipAsset.h"
#include "gameplay/ScenePhysics.h"
#include "ui/CanvasLayout.h"

#include "gameplay/CharacterController.h"
#include "gameplay/GameActionBinding.h"
#include "gameplay/SceneCameraUtils.h"
#include "graphics/camera/Camera.h"
#include "math/TransformUtils.h"
#include "physics/ColliderUtils.h"
#include "physics/CollisionWorld.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneAudio.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneLighting.h"
#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <string_view>

std::vector<SceneValidationIssue>
SceneValidation::Inspect(const Scene &scene, const CameraPose &fallbackPose,
                         bool resolveInitialPose,
                         const CollisionWorld *previousWorld) {
	std::vector<SceneValidationIssue> issues;
	const auto add = [&](SceneObjectId id, std::string_view reason) {
		const auto *object = scene.FindObject(id);
		issues.push_back(
		    {id, object
		             ? std::format(
		                   "{} Object: \"{}\" (ID {}).", reason,
		                   object->name.empty() ? "Object" : object->name, id)
		             : std::string(reason)});
	};
	SceneObjectId firstCharacter = 0, primary = 0;
	SceneActivity::States active;

	if (!SceneActivity::BuildStates(scene, active)) {
		add(0, "Parent hierarchy is invalid.");
		return issues;
	}

	SceneHierarchy::WorldTransforms sourceTransforms;

	if (!SceneHierarchy::BuildWorldTransforms(scene, sourceTransforms)) {
		add(0, "Parent hierarchy or composed world transform is invalid.");
		return issues;
	}

	std::vector<PointLightData> lights;
	std::string lightError;
	SceneObjectId invalidLight = 0;
	DirectionalLightsData directionalLights;
	ShadowSettings shadows;
	if (!SceneLighting::CollectDirectionalLights(scene, sourceTransforms, active,
	                                             LightingSettings{}, directionalLights,
	                                             shadows, lightError, &invalidLight)) {
		add(invalidLight, lightError);
	}

	if (!SceneLighting::CollectPointLights(scene, sourceTransforms, active,
	                                       lights, lightError, &invalidLight)) {
		add(invalidLight, lightError);
	}

	std::vector<SpotLightData> spotLights;

	if (!SceneLighting::CollectSpotLights(scene, sourceTransforms, active,
	                                      spotLights, lightError, &invalidLight)) {
		add(invalidLight, lightError);
	}

	std::string physicsError;
	SceneObjectId invalidPhysics = 0;
	if (!ScenePhysics::Validate(scene, active, sourceTransforms, physicsError,
	                            &invalidPhysics))
		add(invalidPhysics, physicsError);
	std::vector<UIElement> uiElements;
	std::string uiError;
	SceneObjectId invalidUI = 0;
	if (!CanvasLayout::Collect(scene, 1280, 720, uiElements, uiError,
	                           &invalidUI))
		add(invalidUI, uiError);
	Scene resolved;
	std::string followError;
	std::vector<SceneObjectId> order;
	const bool resolvedInitial =
	    resolveInitialPose
	        ? SceneCameraUtils::ResolveInitialScene(scene, resolved,
	                                                followError)
	        : SceneCameraUtils::GetUpdateOrder(scene, order, followError);
	const bool useResolved = resolvedInitial && resolveInitialPose;
	const auto &initial = useResolved ? resolved : scene;
	SceneHierarchy::WorldTransforms resolvedTransforms;
	if (useResolved &&
	    !SceneHierarchy::BuildWorldTransforms(resolved, resolvedTransforms)) {
		add(0, "Initial world transforms are invalid.");
		return issues;
	}
	const auto &transforms = useResolved ? resolvedTransforms : sourceTransforms;

	for (const auto &[id, object] : scene.GetObjects()) {
		if (!active.at(id)) {
			continue;
		}

		if ((object.mesh && !object.mesh->IsValid()) ||
		    (object.meshRenderer && !object.meshRenderer->IsValid())) {
			add(id, "Mesh or Mesh Renderer settings are invalid.");
		}

		if (object.audioSource && !object.audioSource->IsValid()) {
			add(id, "Audio Source settings or WAV reference are invalid.");
		}

		if (object.script && object.script->enabled) {
			if (!object.script->IsValid())
				add(id, "Lua Script settings are invalid.");
			else if (!object.script->asset.empty())
				for (const auto &[name, target] : object.script->references) {
					(void)name;

					if (target && !scene.Contains(target)) {
						add(id, "A Script object reference points to a missing "
						        "object.");
						break;
					}
				}
		}

		if (object.animator &&
		    (!object.animator->IsValid() || !object.HasMesh()))
			add(id,
			    "Animator requires valid settings and a Mesh with a model.");
		if (object.particleEmitter && !object.particleEmitter->IsValid())
			add(id, "Particle Emitter settings are invalid.");
		if (object.transformAnimation &&
		    (!object.transformAnimation->IsValid() || object.motionPath ||
		     object.character || object.followCamera))
			add(id, "Transform Animation requires valid keys and cannot share "
			        "an object with Motion Path, Character Controller or "
			        "Follow Camera.");
		if (object.motionPath && (!object.motionPath->IsValid() ||
		                          object.character || object.followCamera)) {
			add(id, "Motion Path requires valid points and cannot share an "
			        "object with Character Controller or Follow Camera.");
		}

		if (object.colorTint && !object.colorTint->IsValid()) {
			add(id, "Color Tint settings are invalid.");
		}

		AnimationClipComponent resolvedClip;
		const auto *clip = object.animationClip ? &*object.animationClip : nullptr;
		if (clip && clip->usesAsset) {
			std::string issue;
			if (AnimationClipAsset::Resolve(*clip, resolvedClip, issue))
				clip = &resolvedClip;
			else {
				add(id, issue);
				clip = nullptr;
			}
		}

		if (clip) {
			if (!clip->IsValid()) {
				add(id, "Animation Clip settings are invalid.");
			} else {
				for (const auto &track : clip->tracks) {
					const auto *target = scene.FindObject(track.target);
					if (!target || !target->CanAnimate(track)) {
						add(id, "Animation Clip target is missing a required "
						        "component or has another transform controller.");
						break;
					}
				}
			}
		}

		if (object.triggerActions) {
			if (!object.triggerActions->IsValid()) {
				add(id, "Trigger Actions settings are invalid.");
			} else if (!object.triggerActions->actions.empty()) {
				bool requiresTrigger = false, requiresButton = false;

				for (const auto &action : object.triggerActions->actions) {
					requiresButton |= action.UsesButton();
					requiresTrigger |= action.UsesContact();
				}

				if (requiresTrigger &&
				    (!object.collider || !object.collider->isTrigger))
					add(id, "Contact Trigger Actions require a Collider with "
					        "Is Trigger enabled.");
				if (requiresButton && !object.button)
					add(id,
					    "Button Trigger Actions require a UI Button.");
				for (const auto &action : object.triggerActions->actions) {
					const bool marker = action.event == TriggerAction::Event::ANIMATION_CLIP_MARKER;
					if ((marker || action.event == TriggerAction::Event::ANIMATION_CLIP_FINISHED) &&
					    (!clip || (marker && action.clipTime > clip->duration))) {
						add(id, "Animation Clip events require a clip and markers within its duration.");
						break;
					}

					if (action.event == TriggerAction::Event::INPUT_PRESSED &&
					    action.inputAction.empty()) {
						add(id, "Input Pressed requires a named input action.");
						break;
					}

					if (action.operation ==
					    TriggerAction::Operation::LOAD_SCENE) {
						if (action.scenePath.empty())
							add(id, "Load Scene requires a destination scene "
							        "asset.");
						continue;
					}

					if (!action.UsesTarget())
						continue;
					if (!scene.Contains(action.target)) {
						add(id, "Trigger Actions require an existing target "
						        "for each action.");
						break;
					}

					if (action.UsesDestination() &&
					    !scene.Contains(action.destination)) {
						add(id,
						    "Teleport and Set Checkpoint require an existing "
						    "destination.");
						break;
					}

					const auto &target = *scene.FindObject(action.target);
					if (action.operation ==
					        TriggerAction::Operation::SET_CHECKPOINT &&
					    (!target.character || !target.respawn)) {
						add(id, "Set Checkpoint requires a target with "
						        "Character Controller and Respawn.");
						break;
					}

					if (action.operation ==
					        TriggerAction::Operation::SET_CAMERA &&
					    !target.camera) {
						add(id, "Set Camera requires a target with a Camera "
						        "component.");
						break;
					}

					if (action.IsAnimationClipOperation() &&
					    !target.animationClip) {
						add(id, "Animation Clip controls require a target with Animation Clip.");
						break;
					}

					if (action.IsMotionPathOperation() && !target.motionPath) {
						add(id, "Motion Path Trigger Actions require a target "
						        "with a Motion Path.");
						break;
					}

					if (action.IsTransformAnimationOperation() &&
					    !target.transformAnimation) {
						add(id, "Transform Animation Trigger Actions require a "
						        "target with Transform Animation.");
						break;
					}

					if (action.operation ==
					        TriggerAction::Operation::EMIT_PARTICLES &&
					    !target.particleEmitter) {
						add(id, "Emit Particles requires a target with a "
						        "Particle Emitter.");
						break;
					}

					if (action.operation ==
					        TriggerAction::Operation::LAUNCH_CHARACTER &&
					    !target.character) {
						add(id, "Launch Character requires a target with a "
						        "Character Controller.");
						break;
					}
					if (action.operation ==
					        TriggerAction::Operation::RESPAWN_CHARACTER &&
					    (!target.character || !target.respawn)) {
						add(id, "Respawn Character requires a target with "
						        "Character Controller and Respawn.");
						break;
					}
					if (action.IsAnimatorOperation() && !target.animator) {
						add(id, "Animator Trigger Actions require a target "
						        "with an Animator.");
						break;
					}

					if (action.operation ==
					        TriggerAction::Operation::SEND_SCRIPT_MESSAGE &&
					    (action.messageName.empty() || !target.script ||
					     !target.script->enabled ||
					     target.script->asset.empty())) {
						add(id, "Send Script Message requires a name and a "
						        "target with an enabled Lua Script.");
						break;
					}

					if (action.IsAudioOperation() && !target.audioSource) {
						add(id, "Audio Trigger Actions require an Audio Source "
						        "on their target.");
						break;
					}
				}
			}
		}

		if (object.character) {
			if (firstCharacter) {
				add(id, std::format(
				            "Only one input-controlled Character Controller is "
				            "supported per scene. Conflicts with object ID {}.",
				            firstCharacter));
			} else {
				firstCharacter = id;
			}

			if (!object.collider || object.collider->isTrigger ||
			    !object.character->IsValid()) {
				add(id, "Character Controller requires a non-trigger "
				        "Collider and "
				        "valid movement settings.");
			}
		}

		if (object.camera) {
			if (!object.camera->IsValid() ||
			    !TransformUtils::IsValid(object.transform)) {
				add(id, "Camera settings or transform are invalid.");
			}

			if (object.camera->primary) {
				if (primary) {
					add(id, std::format("Only one Camera can be marked "
					                    "Primary. Conflicts with object ID {}.",
					                    primary));
				} else {
					primary = id;
				}
			}
		}

		if (object.respawn &&
		    (!object.character || !object.respawn->IsValid())) {
			add(id,
			    "Respawn requires a Character Controller and valid settings.");
		}

		AxisAlignedBox body;

		if (object.collider &&
		    !ColliderUtils::GetWorldBounds(*object.collider,
		                                   transforms.at(id).matrix, body)) {
			add(id, "Collider cannot be represented at its transform.");
		}

		if (object.followCamera) {
			Transform cameraTransform;

			// 不正な依存関係は、個別検証で各物体へ報告する。
			const bool valid =
			    resolvedInitial || SceneCameraUtils::GetInitialTransform(
			                           scene, id, cameraTransform);

			if (!valid) {
				add(id, "Follow Camera requires valid Camera settings, a valid "
				        "target and pose, and no circular or broken chain or "
				        "Character Controller on the same object.");
			}
		}
	}

	for (const auto &[id, object] : scene.GetObjects()) {
		if (!active.at(id) || !object.character ||
		    !object.character->IsValid() || !object.collider) {
			continue;
		}

		auto world = previousWorld ? *previousWorld : CollisionWorld{};

		std::vector<SceneObjectId> ignored;

		if (!SceneHierarchy::GetSubtree(initial, id, ignored) ||
		    !world.Rebuild(initial, transforms, active, ignored)) {
			// 不正な障害物は上の一覧で報告し、修正後に主人公の配置を再検証する。
			continue;
		}

		CharacterController controller;

		if (!controller.Initialize(transforms.at(id).matrix, *object.collider,
		                           *object.character, world)) {
			add(id, "The character starts inside a collider or has an invalid "
			        "transform or Collider.");
		}

		if (object.respawn && object.respawn->IsValid() &&
		    (resolveInitialPose || !object.respawn->useStartPosition)) {
			const auto &settings = *object.respawn;
			auto recovery = transforms.at(id).matrix;

			if (!settings.useStartPosition) {
				recovery._41 = settings.position.x;
				recovery._42 = settings.position.y;
				recovery._43 = settings.position.z;
			}

			if ((resolveInitialPose &&
			     transforms.at(id).GetPosition().y <= settings.minimumY) ||
			    recovery._42 <= settings.minimumY ||
			    !controller.Initialize(recovery, *object.collider,
			                           *object.character, world)) {
				add(id, "The character and its respawn point must be above "
				        "Fall Below Y and outside solid colliders.");
			}
		}
	}

	Camera validator;

	if (primary) {
		const auto &object = *scene.FindObject(primary);
		CameraPose pose;

		bool hasPose = true;

		if (resolveInitialPose) {
			hasPose = SceneCameraUtils::GetInitialPose(scene, primary, pose);
		} else {
			pose = SceneCameraUtils::FromWorldTransform(transforms.at(primary));
		}

		if (!hasPose || !validator.SetPose(pose) ||
		    !validator.SetSettings(object.camera->settings)) {
			add(primary, "Camera pose or projection is invalid.");
		}
	} else if (!validator.SetPose(fallbackPose) ||
	           !validator.SetSettings(CameraSettings{})) {
		add(0, "Fallback Camera pose or projection is invalid.");
	}

	if (issues.empty()) {
		CameraPose listenerCamera = fallbackPose;
		if (primary)
			listenerCamera =
			    SceneCameraUtils::FromWorldTransform(transforms.at(primary));
		SceneAudioFrame audioFrame;
		std::string audioError;
		SceneObjectId invalidAudio = 0;
		if (!SceneAudio::Collect(initial, listenerCamera, transforms, active,
		                         audioFrame, audioError, &invalidAudio))
			add(invalidAudio, audioError);
	}

	return issues;
}

std::vector<SceneValidationIssue>
SceneValidation::InspectInputs(const Scene &scene,
                               std::span<const GameActionBinding> bindings) {
	std::vector<SceneValidationIssue> issues;

	for (const auto &[id, object] : scene.GetObjects()) {
		if (!object.triggerActions)
			continue;
		const auto &actions = object.triggerActions->actions;

		for (std::size_t i = 0; i < actions.size(); ++i) {
			const auto &name = actions[i].inputAction;
			if (name.empty())
				continue;
			const auto binding =
			    std::ranges::find(bindings, name, &GameActionBinding::name);
			if (binding != bindings.end() &&
			    (binding->keyboard || binding->mouse || binding->gamepad ||
			     binding->gamepadAxis))
				continue;
			const char *problem =
			    binding == bindings.end()
			        ? "does not exist in Game Settings"
			        : "has no keyboard, mouse or gamepad binding";
			issues.push_back(
			    {.objectId = id,
			     .message = std::format("Named input '{}' {}. Object {} (ID {}), "
			                     "Trigger Action {}.",
			                     name, problem,
			                     object.name.empty() ? "Object" : object.name,
			                     id, i + 1),
			     .inputAction = name});
		}
	}

	return issues;
}
