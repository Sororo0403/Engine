#include "gameplay/GameplayInspector.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "scene/Scene.h"
#include <imgui.h>
#include <string>

namespace {
/// <summary>
/// 1つの入力欄の連続変更を、1回のUndo操作としてまとめる
/// </summary>
/// <param name="history">履歴</param>
/// <param name="id">物体ID</param>
/// <param name="current">現在のコンポーネント</param>
/// <param name="edited">入力後の設定</param>
/// <param name="changed">入力変更の有無</param>
/// <param name="begin">履歴の編集開始関数</param>
/// <returns>true:有効な変更あり false:変更なし</returns>
template <class T>
bool ApplyField(EditorHistory &history, SceneObjectId id,
                std::optional<T> &current, const T &edited, bool changed,
                bool (EditorHistory::*begin)(SceneObjectId)) {
	if (ImGui::IsItemActivated() && !(history.*begin)(id)) {
		return false;
	}

	bool applied = false;

	if (changed && edited.IsValid() && current && *current != edited) {
		if (!history.IsEditing() && !(history.*begin)(id)) {
			return false;
		}

		current = edited;
		applied = true;
	}

	if (ImGui::IsItemDeactivated() && !ImGui::IsAnyItemActive()) {
		history.CommitEdit();
	}

	return applied;
}
} // namespace

bool GameplayInspector::Draw(const ImGuiSystem &ui, const Scene &scene,
                             SceneObjectId id, SceneObject &object,
                             EditorHistory &history) const {
	const auto scope = std::to_string(id);
	ImGui::PushID(scope.c_str());
	ImGui::PushID("GameplayInspector");
	bool changed = false;

	if (object.character && ui.CollapsingHeader("Character Controller")) {
		if (!object.collider) {
			ImGui::TextWrapped("Box Collider is missing.");
		}

		auto field = [&](const char *label, float CharacterSettings::*member,
		                 float maximum) {
			auto edited = *object.character;
			const bool value =
			    ImGui::DragFloat(label, &(edited.*member), 0.05f, 0.01f,
			                     maximum, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ApplyField(history, id, object.character, edited, value,
			                      &EditorHistory::BeginCharacter);
		};
		field("Move Speed", &CharacterSettings::moveSpeed,
		      CharacterSettings::kMaximumSpeed);
		field("Jump Speed", &CharacterSettings::jumpSpeed,
		      CharacterSettings::kMaximumSpeed);
		field("Gravity", &CharacterSettings::gravity,
		      CharacterSettings::kMaximumGravity);
		field("Terminal Speed", &CharacterSettings::terminalSpeed,
		      CharacterSettings::kMaximumSpeed);

		if (ui.Button("Remove Character Controller")) {
			changed |= history.SetCharacter(id, std::nullopt);
		}
	}

	if (object.followCamera && ui.CollapsingHeader("Follow Camera")) {
		if (!object.camera) {
			ImGui::TextWrapped("Camera is missing.");
		}

		const auto *target = scene.FindObject(object.followCamera->target);
		const auto preview = target ? target->name : "Choose target";

		if (ImGui::BeginCombo("Target", preview.c_str())) {
			for (const auto &[targetId, candidate] : scene.GetObjects()) {
				if (targetId == id) {
					continue;
				}

				const auto label =
				    candidate.name + "###" + std::to_string(targetId);

				if (ImGui::Selectable(label.c_str(),
				                      targetId ==
				                          object.followCamera->target)) {
					auto edited = *object.followCamera;
					edited.target = targetId;
					changed |= history.SetFollowCamera(id, edited);
				}
			}

			ImGui::EndCombo();
		}

		if (!target || object.followCamera->target == id) {
			ImGui::TextWrapped(
			    "Choose another object in this scene before Play.");
		}

		auto edited = *object.followCamera;
		bool value = ui.DragFloat3("Camera Offset", edited.offset, 0.01f,
		                           -FollowCameraSettings::kMaximumOffset,
		                           FollowCameraSettings::kMaximumOffset);
		changed |= ApplyField(history, id, object.followCamera, edited, value,
		                      &EditorHistory::BeginFollowCamera);
		edited = *object.followCamera;
		value = ui.DragFloat3("Look Offset", edited.lookOffset, 0.01f,
		                      -FollowCameraSettings::kMaximumOffset,
		                      FollowCameraSettings::kMaximumOffset);
		changed |= ApplyField(history, id, object.followCamera, edited, value,
		                      &EditorHistory::BeginFollowCamera);
		edited = *object.followCamera;
		value = ImGui::DragFloat("Follow Smoothing", &edited.smoothing, 0.05f,
		                         0, 100, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		changed |= ApplyField(history, id, object.followCamera, edited, value,
		                      &EditorHistory::BeginFollowCamera);

		if (ui.Button("Remove Follow Camera")) {
			changed |= history.SetFollowCamera(id, std::nullopt);
		}
	}

	if (object.respawn && ui.CollapsingHeader("Respawn")) {
		if (!object.character) {
			ImGui::TextWrapped("Add a Character Controller before Play.");
		}

		auto edited = *object.respawn;
		bool value =
		    ImGui::DragFloat("Fall Below Y", &edited.minimumY, 0.1f, -100000,
		                     100000, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		changed |= ApplyField(history, id, object.respawn, edited, value,
		                      &EditorHistory::BeginRespawn);
		edited = *object.respawn;

		if (ImGui::Checkbox("Use Start Position", &edited.useStartPosition)) {
			changed |= history.SetRespawn(id, edited);
		}

		if (!object.respawn->useStartPosition) {
			edited = *object.respawn;
			value = ui.DragFloat3("Respawn Position", edited.position, 0.01f,
			                      -100000, 100000);
			changed |= ApplyField(history, id, object.respawn, edited, value,
			                      &EditorHistory::BeginRespawn);

			if (ui.Button("Use Current Position")) {
				edited = *object.respawn;
				edited.position = object.transform.translation;
				changed |= history.SetRespawn(id, edited);
			}
		}

		if (ui.Button("Remove Respawn")) {
			changed |= history.SetRespawn(id, std::nullopt);
		}
	}

	ImGui::PopID();
	ImGui::PopID();
	return changed;
}
