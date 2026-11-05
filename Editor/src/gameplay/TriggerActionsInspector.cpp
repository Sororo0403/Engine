#include "gameplay/TriggerActionsInspector.h"

#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "scene/Scene.h"
#include <array>
#include <format>
#include <imgui.h>
#include <optional>
#include <string>
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
	}
	return "Invalid";
}
} // namespace

bool TriggerActionsInspector::Draw(const ImGuiSystem &ui, const Scene &scene,
                                   SceneObjectId id, SceneObject &object,
                                   EditorHistory &history) const {
	if (!object.triggerActions || !ui.CollapsingHeader("Trigger Actions")) {
		return false;
	}
	const auto scope = std::to_string(id);
	ImGui::PushID(scope.c_str());
	ImGui::PushID("TriggerActionsInspector");
	auto edited = *object.triggerActions;
	bool changed = false;
	std::optional<std::size_t> remove;
	std::optional<std::pair<std::size_t, std::size_t>> move;
	for (std::size_t i = 0; i < edited.actions.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		auto &action = edited.actions[i];
		const auto title = std::format("Action {}", i + 1);
		if (ui.CollapsingHeader(title.c_str())) {
			if (ImGui::BeginCombo("Event",
			                      action.event == TriggerEvent::Type::ENTER
			                          ? "Enter"
			                          : "Exit")) {
				for (const auto event : std::array{TriggerEvent::Type::ENTER,
				                                   TriggerEvent::Type::EXIT}) {
					if (ImGui::Selectable(event == TriggerEvent::Type::ENTER
					                          ? "Enter"
					                          : "Exit",
					                      action.event == event) &&
					    action.event != event) {
						action.event = event;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			const auto target = TargetLabel(scene, action.target);
			if (ImGui::BeginCombo("Target", target.c_str())) {
				if (ImGui::Selectable("None", action.target == 0) &&
				    action.target != 0) {
					action.target = 0;
					changed = true;
				}
				for (const auto &[targetId, candidate] : scene.GetObjects()) {
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
			if (ImGui::BeginCombo("Operation",
			                      OperationLabel(action.operation))) {
				for (const auto operation :
				     std::array{TriggerAction::Operation::ENABLE,
				                TriggerAction::Operation::DISABLE,
				                TriggerAction::Operation::TOGGLE}) {
					if (ImGui::Selectable(OperationLabel(operation),
					                      action.operation == operation) &&
					    action.operation != operation) {
						action.operation = operation;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			changed |= ImGui::Checkbox("Once per Play", &action.once);
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
		    action.event == TriggerEvent::Type::ENTER ? "Enter" : "Exit",
		    TargetLabel(scene, action.target), OperationLabel(action.operation),
		    action.once ? "Yes" : "No"));
	}
}
