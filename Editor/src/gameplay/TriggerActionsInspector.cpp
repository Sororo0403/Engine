#include "gameplay/TriggerActionsInspector.h"
#include "assets/AssetDragDrop.h"
#include "platform/windows/PathUtils.h"

#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "scene/Scene.h"
#include <array>
#include <format>
#include <imgui.h>
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
    std::span<const std::filesystem::path> scenes) const {
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
				                TriggerAction::Event::INTERACT}) {
					if (ImGui::Selectable(EventLabel(event),
					                      action.event == event) &&
					    action.event != event) {
						action.event = event;
						if (!action.AllowsDynamicContact() &&
						    action.contact ==
						        TriggerAction::Contact::DYNAMIC_BODY)
							action.contact = TriggerAction::Contact::ANY;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			if (ImGui::BeginCombo("Contact", ContactLabel(action.contact))) {
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
			if (ImGui::BeginCombo("Contact Tag",
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
			if (action.operation != TriggerAction::Operation::LOAD_SCENE) {
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
				for (const auto operation :
				     std::array{TriggerAction::Operation::ENABLE,
				                TriggerAction::Operation::DISABLE,
				                TriggerAction::Operation::TOGGLE,
				                TriggerAction::Operation::PLAY_AUDIO,
				                TriggerAction::Operation::STOP_AUDIO,
				                TriggerAction::Operation::TELEPORT,
				                TriggerAction::Operation::SET_CHECKPOINT,
				                TriggerAction::Operation::SET_CAMERA,
				                TriggerAction::Operation::PLAY_MOTION_PATH,
				                TriggerAction::Operation::REVERSE_MOTION_PATH,
				                TriggerAction::Operation::PAUSE_MOTION_PATH,
				                TriggerAction::Operation::LOAD_SCENE}) {
					if (ImGui::Selectable(OperationLabel(operation),
					                      action.operation == operation) &&
					    action.operation != operation) {
						action.operation = operation;
						if (!action.AllowsDynamicContact() &&
						    action.contact ==
						        TriggerAction::Contact::DYNAMIC_BODY)
							action.contact = TriggerAction::Contact::ANY;
						changed = true;
					}
				}
				ImGui::EndCombo();
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
					if (ImGui::Selectable("None", action.scenePath.empty())) {
						changed |= !action.scenePath.empty();
						action.scenePath.clear();
					}
					for (const auto &path : scenes) {
						if (ImGui::Selectable(PathUtils::ToUtf8(path).c_str(),
						                      path == action.scenePath)) {
							changed |= path != action.scenePath;
							action.scenePath = path;
						}
					}
					ImGui::EndCombo();
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
		edited.actions.emplace_back();
		changed = true;
	}
	ui.EndDisabled();
	const bool removeComponent = ImGui::Button("Remove Trigger Actions");
	ImGui::PopID();
	ImGui::PopID();
	if (removeComponent) {
		return history.SetTriggerActions(id, std::nullopt);
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
		ui.Text(std::format("Contact: {}", ContactLabel(action.contact)));
		if (!action.contactTag.empty())
			ui.Text(std::format("Contact Tag: {}", action.contactTag));
		if (action.operation == TriggerAction::Operation::LOAD_SCENE)
			ui.Text("Scene: " + PathUtils::ToUtf8(action.scenePath));
		if (action.UsesDestination()) {
			ui.Text("Destination: " + TargetLabel(scene, action.destination));
		}
	}
}
