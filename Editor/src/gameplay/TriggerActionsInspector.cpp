#include "gameplay/TriggerActionsInspector.h"
#include "animation/AnimationClipAsset.h"
#include "assets/AssetDragDrop.h"
#include "gameplay/SceneSimulation.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/StringConversion.h"

#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneSerializer.h"
#include <Windows.h>
#include <array>
#include <cfloat>
#include <format>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace {
/// <summary>
/// 未設定や不在も含め、参照先を名前とIDで識別する
/// </summary>
/// <param name="scene">名前を取得するシーン</param>
/// <param name="id">参照先。0は未設定</param>
/// <returns>表示用の文字列</returns>
std::string TargetLabel(const Scene &scene, SceneObjectId id) {
	if (id == 0) {
		return "None";
	}

	const auto *object = scene.FindObject(id);
	return std::format("{} (ID {})", object ? object->name : "Missing", id);
}

/// <summary>
/// 操作種別の表示名を返す
/// </summary>
/// <param name="operation">表示する種別</param>
/// <returns>Inspectorの項目名</returns>
const char *OperationLabel(TriggerAction::Operation operation) {
	switch (operation) {
	case TriggerAction::Operation::ENABLE:
		return "Enable";
	case TriggerAction::Operation::DISABLE:
		return "Disable";
	case TriggerAction::Operation::TOGGLE:
		return "Toggle";
	case TriggerAction::Operation::PLAY_AUDIO:
		return "Play Audio";
	case TriggerAction::Operation::STOP_AUDIO:
		return "Stop Audio";
	case TriggerAction::Operation::TELEPORT:
		return "Teleport";
	case TriggerAction::Operation::RESPAWN_CHARACTER:
		return "Respawn Character";
	case TriggerAction::Operation::LAUNCH_CHARACTER:
		return "Launch Character";
	case TriggerAction::Operation::SET_CHECKPOINT:
		return "Set Checkpoint";
	case TriggerAction::Operation::SET_CAMERA:
		return "Set Camera";
	case TriggerAction::Operation::PLAY_MOTION_PATH:
		return "Play Motion Path";
	case TriggerAction::Operation::REVERSE_MOTION_PATH:
		return "Reverse Motion Path";
	case TriggerAction::Operation::PAUSE_MOTION_PATH:
		return "Pause Motion Path";
	case TriggerAction::Operation::PAUSE_ANIMATION_CLIP:
		return "Pause Animation Clip";
	case TriggerAction::Operation::RESUME_ANIMATION_CLIP:
		return "Resume Animation Clip";
	case TriggerAction::Operation::PLAY_ANIMATION_CLIP:
		return "Play Animation Clip";
	case TriggerAction::Operation::PLAY_TRANSFORM_ANIMATION:
		return "Play Transform Animation";
	case TriggerAction::Operation::RESTART_TRANSFORM_ANIMATION:
		return "Restart Transform Animation";
	case TriggerAction::Operation::PAUSE_TRANSFORM_ANIMATION:
		return "Pause Transform Animation";
	case TriggerAction::Operation::STOP_TRANSFORM_ANIMATION:
		return "Stop Transform Animation";
	case TriggerAction::Operation::PLAY_ANIMATOR:
		return "Play Animator";
	case TriggerAction::Operation::RESTART_ANIMATOR:
		return "Restart Animator";
	case TriggerAction::Operation::PAUSE_ANIMATOR:
		return "Pause Animator";
	case TriggerAction::Operation::STOP_ANIMATOR:
		return "Stop Animator";
	case TriggerAction::Operation::SEND_SCRIPT_MESSAGE:
		return "Send Script Message";
	case TriggerAction::Operation::EMIT_PARTICLES:
		return "Emit Particles";
	case TriggerAction::Operation::PAUSE_GAME:
		return "Pause Game";
	case TriggerAction::Operation::RESUME_GAME:
		return "Resume Game";
	case TriggerAction::Operation::TOGGLE_PAUSE:
		return "Toggle Pause";
	case TriggerAction::Operation::RESTART_SCENE:
		return "Restart Scene";
	case TriggerAction::Operation::QUIT_GAME:
		return "Quit Game";
	case TriggerAction::Operation::LOAD_SCENE:
		return "Load Scene";
	}

	return "Invalid";
}

/// <summary>
/// Trigger Actionsの実行条件を表示名へ変換する
/// </summary>
/// <param name="event">表示する条件</param>
/// <returns>Inspectorの項目名</returns>
const char *EventLabel(TriggerAction::Event event) {
	switch (event) {
	case TriggerAction::Event::ENTER:
		return "Enter";
	case TriggerAction::Event::EXIT:
		return "Exit";
	case TriggerAction::Event::FIRST_ENTER:
		return "First Enter";
	case TriggerAction::Event::LAST_EXIT:
		return "Last Exit";
	case TriggerAction::Event::INTERACT:
		return "Interact";
	case TriggerAction::Event::ANIMATION_CLIP_MARKER:
		return "Animation Clip Marker";
	case TriggerAction::Event::ANIMATION_CLIP_FINISHED:
		return "Animation Clip Finished";
	case TriggerAction::Event::BUTTON_CLICK:
		return "Button Click";
	case TriggerAction::Event::BUTTON_FOCUS:
		return "Button Focus";
	case TriggerAction::Event::INPUT_PRESSED:
		return "Input Pressed";
	}

	return "Invalid";
}

/// <summary>
/// 接触相手の条件をInspectorの表示名へ変換する
/// </summary>
const char *ContactLabel(TriggerAction::Contact contact) {
	switch (contact) {
	case TriggerAction::Contact::ANY:
		return "Any Collider";
	case TriggerAction::Contact::CHARACTER:
		return "Character Controller";
	case TriggerAction::Contact::DYNAMIC_BODY:
		return "Dynamic Rigid Body";
	}

	return "Invalid";
}
} // namespace

bool TriggerActionsInspector::Draw(
    const ImGuiSystem &ui, const Scene &scene, SceneObjectId id,
    SceneObject &object, EditorHistory &history,
    std::filesystem::path &sceneOpenRequest,
    SceneObjectId &sceneOpenObject,
    std::filesystem::path &sceneLocationRequest,
    std::span<const std::filesystem::path> scenes,
    std::span<const GameActionBinding> inputBindings) {
	if (!object.triggerActions || !ui.CollapsingHeader("Trigger Actions")) {
		return false;
	}

	const auto scope = std::to_string(id);
	ImGui::PushID(scope.c_str());
	ImGui::PushID("TriggerActionsInspector");
	auto edited = *object.triggerActions;
	std::set<std::string_view> tags;

	for (const auto &[objectId, candidate] : scene.GetObjects()) {
		(void)objectId;
		if (!candidate.tag.empty())
			tags.insert(candidate.tag);
	}

	bool changed = false;
	std::optional<std::size_t> remove;
	std::optional<std::pair<std::size_t, std::size_t>> move;
	std::optional<float> clipDuration;
	bool checkedClipDuration = false;

	for (std::size_t i = 0; i < edited.actions.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		auto &action = edited.actions[i];
		const auto title = std::format("Action {}", i + 1);

		if (ui.CollapsingHeader(title.c_str())) {
			if (ImGui::BeginCombo("Event", EventLabel(action.event))) {
				for (const auto event :
				     std::array{TriggerAction::Event::ENTER,
				                TriggerAction::Event::EXIT,
				                TriggerAction::Event::FIRST_ENTER,
				                TriggerAction::Event::LAST_EXIT,
				                TriggerAction::Event::INTERACT,
				                TriggerAction::Event::BUTTON_CLICK,
				                TriggerAction::Event::BUTTON_FOCUS,
				                TriggerAction::Event::ANIMATION_CLIP_FINISHED,
				                TriggerAction::Event::ANIMATION_CLIP_MARKER,
				                TriggerAction::Event::INPUT_PRESSED}) {
					if (ImGui::Selectable(EventLabel(event),
					                      action.event == event) &&
					    action.event != event) {
						action.event = event;
						action.clipTime = 0;
						if (event != TriggerAction::Event::INTERACT &&
						    event != TriggerAction::Event::INPUT_PRESSED)
							action.inputAction.clear();
						if (!action.UsesContact()) {
							action.contact = TriggerAction::Contact::ANY;
							action.contactTag.clear();
						}

						if (!action.AllowsDynamicContact() &&
						    action.contact ==
						        TriggerAction::Contact::DYNAMIC_BODY)
							action.contact = TriggerAction::Contact::ANY;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}

			if (action.event == TriggerAction::Event::ANIMATION_CLIP_MARKER) {
				if (!checkedClipDuration && object.animationClip) {
					checkedClipDuration = true;
					if (object.animationClip->usesAsset) {
						AnimationClipComponent clip;
						std::string error;
						if (AnimationClipAsset::Load(object.animationClip->asset, clip, error))
							clipDuration = clip.duration;
					} else {
						clipDuration = object.animationClip->duration;
					}
				}
				changed |= ImGui::DragFloat("Clip Time (s)", &action.clipTime,
				                            .01f, 0.f, clipDuration.value_or(AnimationClipComponent::kMaximumSeconds), "%.3f",
				                            ImGuiSliderFlags_AlwaysClamp);
				if (clipDuration && action.clipTime > *clipDuration) {
					const auto warning = std::format("Marker is past the clip end ({} s).", *clipDuration);
					ImGui::TextWrapped("%s", warning.c_str());
					if (ui.Button("Move Marker to Clip End")) {
						action.clipTime = *clipDuration;
						changed = true;
					}
				}
			}

			const bool interact =
			    action.event == TriggerAction::Event::INTERACT;
			const char *defaultInput = interact ? "Default Interact" : "None";
			if ((interact ||
			     action.event == TriggerAction::Event::INPUT_PRESSED) &&
			    ImGui::BeginCombo("Input", action.inputAction.empty()
			                                   ? defaultInput
			                                   : action.inputAction.c_str())) {
				if (ImGui::Selectable(defaultInput,
				                      action.inputAction.empty()) &&
				    !action.inputAction.empty()) {
					action.inputAction.clear();
					changed = true;
				}
				for (const auto &binding : inputBindings) {
					const auto &name = binding.name;
					ImGui::PushID(name.c_str());
					const auto position = ImGui::GetCursorScreenPos();
					if (ImGui::Selectable("##input",
					                      name == action.inputAction) &&
					    name != action.inputAction) {
						action.inputAction = name;
						changed = true;
					}
					ImGui::GetWindowDrawList()->AddText(
					    position, ImGui::GetColorU32(ImGuiCol_Text),
					    name.c_str());
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}
			if (action.UsesContact() &&
			    ImGui::BeginCombo("Contact", ContactLabel(action.contact))) {
				for (const auto contact :
				     std::array{TriggerAction::Contact::ANY,
				                TriggerAction::Contact::CHARACTER,
				                TriggerAction::Contact::DYNAMIC_BODY}) {
					if (!action.AllowsDynamicContact() &&
					    contact == TriggerAction::Contact::DYNAMIC_BODY)
						continue;
					if (ImGui::Selectable(ContactLabel(contact),
					                      contact == action.contact) &&
					    contact != action.contact) {
						action.contact = contact;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}

			if (action.UsesContact() &&
			    ImGui::BeginCombo("Contact Tag",
			                      action.contactTag.empty()
			                          ? "Any Tag"
			                          : action.contactTag.c_str())) {
				if (ImGui::Selectable("Any Tag##noTag",
				                      action.contactTag.empty()) &&
				    !action.contactTag.empty()) {
					action.contactTag.clear();
					changed = true;
				}
				for (const auto tag : tags) {
					ImGui::PushID(tag.data(), tag.data() + tag.size());
					const auto position = ImGui::GetCursorScreenPos();

					if (ImGui::Selectable("##tag", tag == action.contactTag) &&
					    tag != action.contactTag) {
						action.contactTag = tag;
						changed = true;
					}

					ImGui::GetWindowDrawList()->AddText(
					    position, ImGui::GetColorU32(ImGuiCol_Text), tag.data(),
					    tag.data() + tag.size());
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}
			if (action.UsesTarget()) {
				const auto target = TargetLabel(scene, action.target);

				if (ImGui::BeginCombo("Target", target.c_str())) {
					if (ImGui::Selectable("None", action.target == 0) &&
					    action.target != 0) {
						action.target = 0;
						changed = true;
					}

					for (const auto &[targetId, candidate] :
					     scene.GetObjects()) {
						if (action.IsAudioOperation() &&
						    !candidate.audioSource) {
							continue;
						}

						if (action.operation ==
						        TriggerAction::Operation::SET_CHECKPOINT &&
						    (!candidate.character || !candidate.respawn))
							continue;
						if (action.operation ==
						        TriggerAction::Operation::SET_CAMERA &&
						    !candidate.camera)
							continue;
						if (action.IsMotionPathOperation() &&
						    !candidate.motionPath)
							continue;
						if (action.IsTransformAnimationOperation() &&
						    !candidate.transformAnimation)
							continue;
						if (action.operation ==
						        TriggerAction::Operation::LAUNCH_CHARACTER &&
						    !candidate.character)
							continue;
						if (action.operation ==
						        TriggerAction::Operation::RESPAWN_CHARACTER &&
						    (!candidate.character || !candidate.respawn))
							continue;
						if (action.IsAnimatorOperation() && !candidate.animator)
							continue;
						if (action.operation ==
						        TriggerAction::Operation::SEND_SCRIPT_MESSAGE &&
						    (!candidate.script || !candidate.script->enabled ||
						     candidate.script->asset.empty()))
							continue;
						if (action.operation ==
						        TriggerAction::Operation::EMIT_PARTICLES &&
						    !candidate.particleEmitter)
							continue;
						const auto label = TargetLabel(scene, targetId);
						if (ImGui::Selectable(label.c_str(),
						                      action.target == targetId) &&
						    action.target != targetId) {
							action.target = targetId;
							changed = true;
						}
					}

					ImGui::EndCombo();
				}
			}

			if (ImGui::BeginCombo("Operation",
			                      OperationLabel(action.operation))) {
				for (const auto operation : std::array{
				         TriggerAction::Operation::ENABLE,
				         TriggerAction::Operation::DISABLE,
				         TriggerAction::Operation::TOGGLE,
				         TriggerAction::Operation::PLAY_AUDIO,
				         TriggerAction::Operation::STOP_AUDIO,
				         TriggerAction::Operation::TELEPORT,
				         TriggerAction::Operation::SET_CHECKPOINT,
				         TriggerAction::Operation::LAUNCH_CHARACTER,
				         TriggerAction::Operation::RESPAWN_CHARACTER,
				         TriggerAction::Operation::SET_CAMERA,
				         TriggerAction::Operation::PLAY_MOTION_PATH,
				         TriggerAction::Operation::REVERSE_MOTION_PATH,
				         TriggerAction::Operation::PAUSE_MOTION_PATH,
				         TriggerAction::Operation::PLAY_TRANSFORM_ANIMATION,
				         TriggerAction::Operation::PLAY_ANIMATION_CLIP,
				         TriggerAction::Operation::PAUSE_ANIMATION_CLIP,
				         TriggerAction::Operation::RESUME_ANIMATION_CLIP,
				         TriggerAction::Operation::RESTART_TRANSFORM_ANIMATION,
				         TriggerAction::Operation::PAUSE_TRANSFORM_ANIMATION,
				         TriggerAction::Operation::STOP_TRANSFORM_ANIMATION,
				         TriggerAction::Operation::PLAY_ANIMATOR,
				         TriggerAction::Operation::RESTART_ANIMATOR,
				         TriggerAction::Operation::PAUSE_ANIMATOR,
				         TriggerAction::Operation::STOP_ANIMATOR,
				         TriggerAction::Operation::SEND_SCRIPT_MESSAGE,
				         TriggerAction::Operation::EMIT_PARTICLES,
				         TriggerAction::Operation::LOAD_SCENE,
				         TriggerAction::Operation::QUIT_GAME,
				         TriggerAction::Operation::RESTART_SCENE,
				         TriggerAction::Operation::PAUSE_GAME,
				         TriggerAction::Operation::RESUME_GAME,
				         TriggerAction::Operation::TOGGLE_PAUSE}) {
					if (ImGui::Selectable(OperationLabel(operation),
					                      action.operation == operation) &&
					    action.operation != operation) {
						action.operation = operation;

						if (!action.UsesTarget()) {
							action.target = 0;
							action.destination = 0;
						}

						if (operation != TriggerAction::Operation::LOAD_SCENE)
							action.arrivalTag.clear();
						if (operation != TriggerAction::Operation::SET_CAMERA)
							action.cameraBlendSeconds = 0;
						if (operation !=
						    TriggerAction::Operation::SEND_SCRIPT_MESSAGE)
							action.messageName.clear();
						if (!action.AllowsDynamicContact() &&
						    action.contact ==
						        TriggerAction::Contact::DYNAMIC_BODY)
							action.contact = TriggerAction::Contact::ANY;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}

			if (action.operation ==
			    TriggerAction::Operation::SEND_SCRIPT_MESSAGE) {
				auto name = action.messageName;
				if (ImGui::InputText("Message Name", &name,
				                     ImGuiInputTextFlags_EnterReturnsTrue) &&
				    (name.empty() || ScriptMessage::IsValidName(name)) &&
				    name != action.messageName) {
					action.messageName = std::move(name);
					changed = true;
				}
			}

			if (action.UsesDestination()) {
				const auto destination = TargetLabel(scene, action.destination);

				if (ImGui::BeginCombo("Destination", destination.c_str())) {
					if (ImGui::Selectable("None", action.destination == 0) &&
					    action.destination != 0) {
						action.destination = 0;
						changed = true;
					}

					for (const auto &[targetId, candidate] :
					     scene.GetObjects()) {
						(void)candidate;
						const auto label = TargetLabel(scene, targetId);
						if (ImGui::Selectable(label.c_str(),
						                      action.destination == targetId) &&
						    action.destination != targetId) {
							action.destination = targetId;
							changed = true;
						}
					}

					ImGui::EndCombo();
				}
			}

			if (action.operation == TriggerAction::Operation::LOAD_SCENE) {
				const auto label =
				    action.scenePath.empty()
				        ? "None"
				        : PathUtils::ToUtf8(action.scenePath.filename());
				if (ImGui::BeginCombo("Destination Scene", label.c_str())) {
					if (ImGui::IsWindowAppearing()) {
						sceneSearch_.clear();
						ImGui::SetKeyboardFocusHere();
					}
					const bool submitted = ImGui::InputTextWithHint(
					    "##SceneSearch", "Search scene paths", &sceneSearch_,
					    ImGuiInputTextFlags_EnterReturnsTrue);
					ImGui::SetItemTooltip("Press Enter to select the only matching scene.");
					if (ImGui::Selectable("None", action.scenePath.empty())) {
						changed |= !action.scenePath.empty();
						action.scenePath.clear();
					}

					const auto search = StringConversion::Utf8ToWide(sceneSearch_);
					std::size_t matchCount = 0;
					const std::filesystem::path *firstMatch = nullptr;
					for (const auto &path : scenes) {
						if (!sceneSearch_.empty()) {
							const auto &native = path.native();
							if (search.empty() ||
							    FindStringOrdinal(FIND_FROMSTART, native.c_str(),
							                      static_cast<int>(native.size()), search.c_str(),
							                      static_cast<int>(search.size()), TRUE) < 0) {
								continue;
							}
						}
						if (matchCount++ == 0) {
							firstMatch = &path;
						}
						if (ImGui::Selectable(PathUtils::ToUtf8(path).c_str(),
						                      path == action.scenePath)) {
							changed |= path != action.scenePath;
							action.scenePath = path;
						}
					}
					if (submitted && !sceneSearch_.empty() && matchCount == 1) {
						changed |= *firstMatch != action.scenePath;
						action.scenePath = *firstMatch;
						ImGui::CloseCurrentPopup();
					} else if (matchCount == 0) {
						ImGui::TextDisabled("No matching scenes.");
					}

					ImGui::EndCombo();
				} else if (!action.scenePath.empty()) {
					ImGui::SetItemTooltip("%s", PathUtils::ToUtf8(action.scenePath).c_str());
				}

				ImGui::Button("Drop Scene Asset");
				if (const auto path =
				        AssetDragDrop::AcceptTarget(AssetDragDrop::Kind::SCENE);
				    !path.empty() && path != action.scenePath) {
					action.scenePath = path;
					changed = true;
				}
				if (!action.scenePath.empty() && ImGui::IsItemHovered())
					ImGui::SetTooltip(
					    "%s", PathUtils::ToUtf8(action.scenePath).c_str());
				ui.SameLine();
				ui.BeginDisabled(action.scenePath.empty());
				if (ui.Button("Open Scene")) {
					sceneOpenRequest = action.scenePath;
					sceneOpenObject = 0;
				}
				ui.SameLine();
				if (ui.Button("Show Scene in Explorer")) {
					sceneLocationRequest = action.scenePath;
				}
				ui.EndDisabled();
				auto tag = action.arrivalTag;
				if (ImGui::InputText("Arrival Tag", &tag,
				                     ImGuiInputTextFlags_EnterReturnsTrue) &&
				    ObjectTag::IsValid(tag) && tag != action.arrivalTag) {
					action.arrivalTag = std::move(tag);
					changed = true;
				}
				ui.BeginDisabled(action.scenePath.empty());
				if (ui.Button("Choose Arrival Tag")) {
					arrivalTags_.clear();
					arrivalTagSearch_.clear();
					Scene destination;
					arrivalTagsLoaded_ = SceneSerializer::Load(destination, action.scenePath);
					if (arrivalTagsLoaded_) {
						std::map<std::string, SceneObjectId> uniqueTargets;
						for (const auto &[targetId, target] : destination.GetObjects()) {
							if (!target.tag.empty() && SceneActivity::IsActive(destination, targetId)) {
								auto [entry, inserted] = uniqueTargets.try_emplace(target.tag, targetId);
								if (!inserted) {
									entry->second = 0;
								}
							}
						}
						for (const auto &[tagName, targetId] : uniqueTargets) {
							if (targetId != 0) {
								arrivalTags_.push_back({tagName, TargetLabel(destination, targetId)});
							}
						}
					}
					ImGui::OpenPopup("Arrival Tags");
				}
				ui.EndDisabled();
				ImGui::SetItemTooltip("List tags used by exactly one active object in the saved scene.");
				ImGui::SetNextWindowSizeConstraints(
				    {0.0f, 0.0f}, {FLT_MAX, 14.0f * ImGui::GetTextLineHeightWithSpacing()});
				if (ImGui::BeginPopup("Arrival Tags")) {
					if (!arrivalTagsLoaded_) {
						ImGui::TextUnformatted("Could not read the destination scene.");
					} else if (arrivalTags_.empty()) {
						ImGui::TextUnformatted("No unique tags on active objects.");
					} else {
						if (ImGui::IsWindowAppearing()) {
							ImGui::SetKeyboardFocusHere();
						}
						const bool submitted = ImGui::InputTextWithHint(
						    "##ArrivalTagSearch", "Search tags or objects", &arrivalTagSearch_,
						    ImGuiInputTextFlags_EnterReturnsTrue);
						ImGui::SetItemTooltip("Press Enter to choose the only matching tag.");
						const auto search = StringConversion::Utf8ToWide(arrivalTagSearch_);
						std::size_t matchCount = 0;
						const ArrivalTagChoice *firstMatch = nullptr;
						for (const auto &candidate : arrivalTags_) {
							const auto choiceLabel =
							    std::format("{} / {}", candidate.tag, candidate.objectLabel);
							if (!arrivalTagSearch_.empty()) {
								const auto text = StringConversion::Utf8ToWide(choiceLabel);
								if (search.empty() ||
								    FindStringOrdinal(FIND_FROMSTART, text.data(),
								                      static_cast<int>(text.size()), search.data(),
								                      static_cast<int>(search.size()), TRUE) < 0) {
									continue;
								}
							}
							if (matchCount++ == 0) {
								firstMatch = &candidate;
							}
							ImGui::PushID(candidate.tag.c_str());
							const auto position = ImGui::GetCursorScreenPos();
							const auto size = ImGui::CalcTextSize(choiceLabel.c_str());
							if (ImGui::Selectable("##ArrivalTag", candidate.tag == action.arrivalTag,
							                      ImGuiSelectableFlags_None, size)) {
								changed |= action.arrivalTag != candidate.tag;
								action.arrivalTag = candidate.tag;
							}
							ImGui::GetWindowDrawList()->AddText(
							    position, ImGui::GetColorU32(ImGuiCol_Text), choiceLabel.c_str());
							ImGui::PopID();
						}
						if (submitted && !arrivalTagSearch_.empty() && matchCount == 1) {
							changed |= action.arrivalTag != firstMatch->tag;
							action.arrivalTag = firstMatch->tag;
							ImGui::CloseCurrentPopup();
						} else if (matchCount == 0) {
							ImGui::TextDisabled("No matching arrival tags.");
						}
					}
					ImGui::EndPopup();
				}
				ui.BeginDisabled(action.scenePath.empty());
				if (ui.Button("Check Destination")) {
					Scene destination;
					std::string message;
					SceneObjectId invalidObject = 0;
					if (!SceneSerializer::Load(destination, action.scenePath)) {
						message = "Could not read the destination scene.";
					} else if (SceneSimulation::ValidateArrival(
					               destination, action.arrivalTag, message, {}, &invalidObject)) {
						message = action.arrivalTag.empty()
						              ? "Destination is valid with its saved placement."
						              : "Tagged arrival is valid.";
					}
					arrivalCheck_ = ArrivalCheck{id, i, action.scenePath,
					                             action.arrivalTag, std::move(message), invalidObject};
				}
				ui.EndDisabled();
				ImGui::SetItemTooltip(
				    "Check the saved scene and arrival placement without running it.");
				if (arrivalCheck_ && arrivalCheck_->objectId == id &&
				    arrivalCheck_->actionIndex == i &&
				    arrivalCheck_->path == action.scenePath &&
				    arrivalCheck_->tag == action.arrivalTag) {
					ImGui::TextWrapped("Last check: %s", arrivalCheck_->message.c_str());
					if (arrivalCheck_->invalidObject != 0 && ui.Button("Show Destination Object")) {
						sceneOpenRequest = arrivalCheck_->path;
						sceneOpenObject = arrivalCheck_->invalidObject;
					}
				}
			}

			if (action.operation == TriggerAction::Operation::SET_CAMERA) {
				float blend = action.cameraBlendSeconds;
				if (ImGui::InputFloat("Blend (s)", &blend, 0, 0, "%.3f",
				                      ImGuiInputTextFlags_EnterReturnsTrue) &&
				    std::isfinite(blend) && blend >= 0 &&
				    blend <= TriggerAction::kMaximumCameraBlendSeconds &&
				    blend != action.cameraBlendSeconds) {
					action.cameraBlendSeconds = blend;
					changed = true;
				}
			}

			if (action.operation ==
			    TriggerAction::Operation::LAUNCH_CHARACTER) {
				float speed = action.launchSpeed;
				if (ImGui::InputFloat("Launch Speed", &speed, 0, 0, "%.3f",
				                      ImGuiInputTextFlags_EnterReturnsTrue) &&
				    std::isfinite(speed) && speed > 0 &&
				    speed <= CharacterSettings::kMaximumSpeed &&
				    speed != action.launchSpeed) {
					action.launchSpeed = speed;
					changed = true;
				}
			}

			float delay = action.delaySeconds;
			if (ImGui::InputFloat("Delay (s)", &delay, 0, 0, "%.3f",
			                      ImGuiInputTextFlags_EnterReturnsTrue) &&
			    std::isfinite(delay) && delay >= 0 &&
			    delay <= TriggerAction::kMaximumDelaySeconds &&
			    delay != action.delaySeconds) {
				action.delaySeconds = delay;
				changed = true;
			}
			changed |= ImGui::Checkbox("Once per Scene", &action.once);
			ui.BeginDisabled(i == 0);

			if (ImGui::SmallButton("Move Up")) {
				move = std::pair{i, i - 1};
			}

			ui.EndDisabled();
			ui.SameLine();
			ui.BeginDisabled(i + 1 == edited.actions.size());

			if (ImGui::SmallButton("Move Down")) {
				move = std::pair{i, i + 1};
			}

			ui.EndDisabled();
			ui.SameLine();

			if (ImGui::SmallButton("Remove Action")) {
				remove = i;
			}
		}

		ImGui::PopID();
	}

	if (remove) {
		edited.actions.erase(edited.actions.begin() +
		                     static_cast<std::ptrdiff_t>(*remove));
		changed = true;
	} else if (move) {
		std::swap(edited.actions[move->first], edited.actions[move->second]);
		changed = true;
	}

	ui.BeginDisabled(edited.actions.size() >=
	                 TriggerActionsComponent::kMaximumActions);
	if (ImGui::Button("Add Action")) {
		auto &action = edited.actions.emplace_back();
		if (object.animationClip)
			action.event = TriggerAction::Event::ANIMATION_CLIP_FINISHED;
		else if (object.button && (!object.collider || !object.collider->isTrigger))
			action.event = TriggerAction::Event::BUTTON_CLICK;
		changed = true;
	}

	ui.EndDisabled();
	const bool removeComponent = ImGui::Button("Remove Trigger Actions");
	ImGui::PopID();
	ImGui::PopID();

	if (removeComponent) {
		arrivalCheck_.reset();
		return history.SetTriggerActions(id, std::nullopt);
	}

	if (changed) {
		arrivalCheck_.reset();
	}
	return changed && history.SetTriggerActions(id, std::move(edited));
}

void TriggerActionsInspector::DrawReadOnly(const ImGuiSystem &ui,
                                           const Scene &scene,
                                           const SceneObject &object) const {
	if (!object.triggerActions || !ui.CollapsingHeader("Trigger Actions")) {
		return;
	}

	for (std::size_t i = 0; i < object.triggerActions->actions.size(); ++i) {
		const auto &action = object.triggerActions->actions[i];
		ui.Text(std::format(
		    "Action {}: {} / {} / {} / Once: {}", i + 1,
		    EventLabel(action.event), TargetLabel(scene, action.target),
		    OperationLabel(action.operation), action.once ? "Yes" : "No"));
		if (action.UsesContact())
			ui.Text(std::format("Contact: {}", ContactLabel(action.contact)));
		if (action.cameraBlendSeconds > 0)
			ui.Text(std::format("Blend: {:.3f} s", action.cameraBlendSeconds));
		if (action.delaySeconds > 0)
			ui.Text(std::format("Delay: {:.3f} s", action.delaySeconds));
		if (action.event == TriggerAction::Event::INTERACT ||
		    action.event == TriggerAction::Event::INPUT_PRESSED)
			ui.Text("Input: " + (action.inputAction.empty()
			                         ? "Default Interact"
			                         : action.inputAction));
		if (!action.contactTag.empty())
			ui.Text(std::format("Contact Tag: {}", action.contactTag));
		if (action.operation == TriggerAction::Operation::SEND_SCRIPT_MESSAGE)
			ui.Text("Message: " + action.messageName);
		if (action.operation == TriggerAction::Operation::LOAD_SCENE)
			ui.Text("Scene: " + PathUtils::ToUtf8(action.scenePath));
		if (action.operation == TriggerAction::Operation::LAUNCH_CHARACTER)
			ui.Text(std::format("Launch Speed: {:.3f}", action.launchSpeed));
		if (!action.arrivalTag.empty())
			ui.Text("Arrival Tag: " + action.arrivalTag);
		if (action.UsesDestination()) {
			ui.Text("Destination: " + TargetLabel(scene, action.destination));
		}
	}
}
