#include "gameplay/GameplayInspector.h"
#include "gameplay/CharacterJumpPreview.h"
#include "gameplay/RespawnPreview.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "physics/AxisAlignedBox.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
#include <format>
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

/// <summary>
/// 開いたときだけ実際のジャンプを予測し、方向別の終了理由・時間・上昇量・水平距離を表示する
/// </summary>
/// <param name="ui">描画用UI</param>
/// <param name="scene">変更しない編集シーン</param>
/// <param name="id">Inspectorに表示する主人公。選択状態には依存しない</param>
/// <returns>選択を要求した着地先のCollider。要求なしは0。選択の変更は描画後に行う</returns>
SceneObjectId DrawJumpMeasurements(const ImGuiSystem &ui, const Scene &scene,
                                   SceneObjectId id) {
	if (!ui.CollapsingHeader("Jump Measurements", false)) {
		return 0;
	}

	CharacterJumpPreview::Paths paths;

	if (!CharacterJumpPreview{}.BuildPaths(scene, id, paths)) {
		ui.Text("Jump measurements unavailable.");
		return 0;
	}

	if (!ImGui::BeginTable("JumpMeasurements", 6,
	                       ImGuiTableFlags_RowBg |
	                           ImGuiTableFlags_BordersInnerV |
	                           ImGuiTableFlags_ScrollX,
	                       {0, 0}, 760.0f)) {
		return 0;
	}

	ImGui::TableSetupColumn("Direction");
	ImGui::TableSetupColumn("Result");
	ImGui::TableSetupColumn("Time (s)");
	ImGui::TableSetupColumn("Height");
	ImGui::TableSetupColumn("Distance XZ");
	ImGui::TableSetupColumn("Landing Collider");
	ImGui::TableHeadersRow();
	SceneObjectId selectedLanding = 0;
	const std::array<const char *, CharacterJumpPreview::kDirectionCount>
	    labels{"In Place", "+X",      "-X",      "+Z",     "-Z",
	           "+X / +Z",  "+X / -Z", "-X / +Z", "-X / -Z"};

	for (std::size_t i = 0; i < paths.size(); ++i) {
		const auto &path = paths[i];
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ui.Text(labels[i]);
		ImGui::TableNextColumn();
		ui.Text(path.end == CharacterJumpPreview::End::LANDED ? "Landed"
		        : path.end == CharacterJumpPreview::End::FALL_LIMIT
		            ? "Fall Limit"
		            : "Time Limit");
		ImGui::TableNextColumn();
		ui.Text(std::format("{:.3f}", path.durationSeconds));
		ImGui::TableNextColumn();
		ui.Text(std::format("{:.3f}", path.maximumHeight));
		ImGui::TableNextColumn();
		ui.Text(std::format("{:.3f}", path.horizontalDistance));
		ImGui::TableNextColumn();
		const auto *landing = path.end == CharacterJumpPreview::End::LANDED
		                          ? scene.FindObject(path.landingObject)
		                          : nullptr;

		if (landing && landing->collider) {
			ImGui::PushID(static_cast<int>(i));

			if (ui.Button("Select Landing Collider")) {
				selectedLanding = path.landingObject;
			}

			ImGui::PopID();
			ui.Text(
			    std::format("{} (ID {})", landing->name, path.landingObject));
		} else {
			ui.Text("-");
		}
	}

	ImGui::EndTable();
	return selectedLanding;
}

} // namespace

bool GameplayInspector::Draw(const ImGuiSystem &ui, const Scene &scene,
                             SceneObjectId id, SceneObject &object,
                             EditorHistory &history,
                             SceneObjectId &targetSelectionRequest,
                             bool canPasteTransform, bool canPasteCharacter,
                             bool canPasteFollow, bool canPasteRespawn) {
	targetSelectionRequest = 0;
	characterRequest_ = {};
	followRequest_ = {};
	respawnRequest_ = {};
	respawnPasteRequest_ = 0;
	respawnFrameRequest_ = 0;
	jumpFrameRequest_ = 0;
	respawnBlockersRequest_ = 0;
	const auto scope = std::to_string(id);
	ImGui::PushID(scope.c_str());
	ImGui::PushID("GameplayInspector");
	bool changed = false;

	if (object.character && ui.CollapsingHeader("Character Controller")) {
		if (!object.collider) {
			ImGui::TextWrapped("Box Collider is missing.");
		}

		auto field = [&](const char *label, float CharacterSettings::*member,
		                 float maximum, float minimum = 0.01f,
		                 float speed = 0.05f, const char *format = "%.2f") {
			auto edited = *object.character;
			const bool value =
			    ImGui::DragFloat(label, &(edited.*member), speed, minimum,
			                     maximum, format, ImGuiSliderFlags_AlwaysClamp);
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
		field("Coyote Time (s)", &CharacterSettings::coyoteTime,
		      CharacterSettings::kMaximumJumpGraceSeconds, 0.0f, 0.005f,
		      "%.3f");
		field("Jump Buffer Time (s)", &CharacterSettings::jumpBufferTime,
		      CharacterSettings::kMaximumJumpGraceSeconds, 0.0f, 0.005f,
		      "%.3f");

		ui.BeginDisabled(!object.collider || !object.character->IsValid());

		if (ui.Button("Select Ground Collider")) {
			characterRequest_ = {CharacterAction::SELECT_GROUND, id};
		}

		if (ui.Button("Frame Jump")) {
			jumpFrameRequest_ = id;
		}

		ui.EndDisabled();

		targetSelectionRequest = DrawJumpMeasurements(ui, scene, id);

		ui.BeginDisabled(!object.character->IsValid());

		if (ui.Button("Copy Character Settings")) {
			characterRequest_ = {CharacterAction::COPY, id};
		}

		ui.EndDisabled();
		ui.SameLine();
		ui.BeginDisabled(!canPasteCharacter);

		if (ui.Button("Paste Character Settings")) {
			characterRequest_ = {CharacterAction::PASTE, id};
		}

		ui.EndDisabled();

		if (ui.Button("Reset Character Settings")) {
			const auto before = object.character;
			changed |= history.ResetComponentSettings(
			               id, EditorHistory::ComponentSettings::CHARACTER) &&
			           object.character != before;
		}

		if (ui.Button("Remove Character Controller")) {
			changed |= history.SetCharacter(id, std::nullopt);
		}
	}

	if (object.followCamera && ui.CollapsingHeader("Follow Camera")) {
		if (!object.camera) {
			ImGui::TextWrapped("Camera is missing.");
		}

		if (const auto target =
		        targetPicker_.Draw(ui, scene, id, object.followCamera->target);
		    target && *target != object.followCamera->target) {
			auto edited = *object.followCamera;
			edited.target = *target;
			changed |= history.SetFollowCamera(id, edited);
		}

		const auto *currentTarget =
		    scene.FindObject(object.followCamera->target);

		if (!currentTarget || object.followCamera->target == id) {
			ImGui::TextWrapped(
			    "Choose another object in this scene before Play.");
		}

		ui.BeginDisabled(!currentTarget || object.followCamera->target == id);

		if (ui.Button("Select Target")) {
			targetSelectionRequest = object.followCamera->target;
		}

		ui.EndDisabled();

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

		edited = *object.followCamera;
		value = ImGui::Checkbox("Avoid Obstacles", &edited.avoidObstacles);
		changed |= ApplyField(history, id, object.followCamera, edited, value,
		                      &EditorHistory::BeginFollowCamera);
		edited = *object.followCamera;
		ui.BeginDisabled(!edited.avoidObstacles);
		value = ImGui::DragFloat("Camera Clearance", &edited.clearance, 0.01f,
		                         FollowCameraSettings::kMinimumClearance,
		                         FollowCameraSettings::kMaximumClearance,
		                         "%.2f", ImGuiSliderFlags_AlwaysClamp);
		changed |= ApplyField(history, id, object.followCamera, edited, value,
		                      &EditorHistory::BeginFollowCamera);
		ui.EndDisabled();

		edited = *object.followCamera;
		value = ImGui::Checkbox("Orbit Input", &edited.orbit.enabled);
		changed |= ApplyField(history, id, object.followCamera, edited, value,
		                      &EditorHistory::BeginFollowCamera);

		if (object.followCamera->orbit.enabled) {
			// 各連続編集を既存のFollow Camera履歴へまとめる。
			const auto orbitField =
			    [&](const char *label, float OrbitCameraSettings::*member,
			        float speed, float minimum, float maximum) {
				    auto next = *object.followCamera;
				    const bool fieldChanged = ImGui::DragFloat(
				        label, &(next.orbit.*member), speed, minimum, maximum,
				        "%.2f", ImGuiSliderFlags_AlwaysClamp);
				    return ApplyField(history, id, object.followCamera, next,
				                      fieldChanged,
				                      &EditorHistory::BeginFollowCamera);
			    };
			changed |= orbitField("Mouse Sensitivity",
			                      &OrbitCameraSettings::mouseSensitivity, 0.01f,
			                      0.01f, 10);
			changed |= orbitField("Stick Degrees Per Second",
			                      &OrbitCameraSettings::stickSpeed, 1, 1, 720);
			changed |= orbitField("Zoom Sensitivity",
			                      &OrbitCameraSettings::zoomSensitivity, 0.01f,
			                      0.01f, 5);
			changed |=
			    orbitField("Minimum Pitch", &OrbitCameraSettings::minimumPitch,
			               0.5f, -89, object.followCamera->orbit.maximumPitch);
			changed |=
			    orbitField("Maximum Pitch", &OrbitCameraSettings::maximumPitch,
			               0.5f, object.followCamera->orbit.minimumPitch, 89);
			changed |= orbitField(
			    "Minimum Distance", &OrbitCameraSettings::minimumDistance,
			    0.05f, 0.01f, object.followCamera->orbit.maximumDistance);
			changed |= orbitField(
			    "Maximum Distance", &OrbitCameraSettings::maximumDistance,
			    0.05f, object.followCamera->orbit.minimumDistance, 10000);
		}

		ui.BeginDisabled(!object.camera || !object.camera->IsValid() ||
		                 !object.followCamera->IsValid() || !currentTarget ||
		                 object.followCamera->target == id);
		if (ui.Button("Frame Follow Path")) {
			followRequest_ = {FollowAction::FRAME_PATH, id};
		}
		ui.EndDisabled();

		ui.BeginDisabled(!object.followCamera->IsValid());

		if (ui.Button("Copy Follow Settings")) {
			followRequest_ = {FollowAction::COPY, id};
		}

		ui.EndDisabled();
		ui.SameLine();
		ui.BeginDisabled(!canPasteFollow);

		if (ui.Button("Paste Follow Settings")) {
			followRequest_ = {FollowAction::PASTE, id};
		}

		ui.EndDisabled();

		if (ui.Button("Reset Follow Settings")) {
			const auto before = object.followCamera;
			changed |=
			    history.ResetComponentSettings(
			        id, EditorHistory::ComponentSettings::FOLLOW_CAMERA) &&
			    object.followCamera != before;
		}

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
				SceneWorldTransform world;
				if (SceneHierarchy::GetWorldTransform(scene, id, world)) {
					edited.position = world.GetPosition();
					changed |= history.SetRespawn(id, edited);
				}
			}
		}

		RespawnPreview::Geometry respawnGeometry;
		ui.BeginDisabled(
		    object.respawn->useStartPosition ||
		    !RespawnPreview{}.BuildGeometry(scene, id, respawnGeometry) ||
		    !respawnGeometry.canRespawn);

		if (ui.Button("Move to Respawn Position")) {
			respawnRequest_ = {RespawnAction::MOVE_TO_POSITION, id};
		}

		ui.EndDisabled();

		ui.BeginDisabled(!canPasteTransform);

		if (ui.Button("Paste Respawn Position")) {
			respawnPasteRequest_ = id;
		}

		ui.EndDisabled();

		std::array<DirectX::XMFLOAT3, 8> frameBounds;
		ui.BeginDisabled(
		    !RespawnPreview::GetFrameBounds(scene, id, frameBounds));

		if (ui.Button("Frame Respawn")) {
			respawnFrameRequest_ = id;
		}

		ui.EndDisabled();

		AxisAlignedBox respawnBody;
		ui.BeginDisabled(
		    !RespawnPreview::GetColliderBounds(scene, id, respawnBody));

		if (ui.Button("Select Blocking Colliders")) {
			respawnBlockersRequest_ = id;
		}

		ui.EndDisabled();

		ui.BeginDisabled(!object.respawn->IsValid());

		if (ui.Button("Copy Respawn Settings")) {
			respawnRequest_ = {RespawnAction::COPY, id};
		}

		ui.EndDisabled();
		ui.SameLine();
		ui.BeginDisabled(!canPasteRespawn);

		if (ui.Button("Paste Respawn Settings")) {
			respawnRequest_ = {RespawnAction::PASTE, id};
		}

		ui.EndDisabled();

		if (ui.Button("Reset Respawn Settings")) {
			const auto before = object.respawn;
			changed |= history.ResetComponentSettings(
			               id, EditorHistory::ComponentSettings::RESPAWN) &&
			           object.respawn != before;
		}

		if (ui.Button("Remove Respawn")) {
			changed |= history.SetRespawn(id, std::nullopt);
		}
	}

	ImGui::PopID();
	ImGui::PopID();
	return changed;
}

void GameplayInspector::Clear() noexcept {
	targetPicker_.Clear();
	followRequest_ = {};
	respawnRequest_ = {};
	characterRequest_ = {};
	respawnPasteRequest_ = 0;
	respawnFrameRequest_ = 0;
	jumpFrameRequest_ = 0;
	respawnBlockersRequest_ = 0;
}

GameplayInspector::CharacterRequest
GameplayInspector::GetCharacterRequest() const {
	return characterRequest_;
}

GameplayInspector::FollowRequest GameplayInspector::GetFollowRequest() const {
	return followRequest_;
}

SceneObjectId GameplayInspector::GetRespawnFrameRequest() const {
	return respawnFrameRequest_;
}

SceneObjectId GameplayInspector::GetRespawnBlockersRequest() const {
	return respawnBlockersRequest_;
}

SceneObjectId GameplayInspector::GetRespawnPasteRequest() const {
	return respawnPasteRequest_;
}

GameplayInspector::RespawnRequest GameplayInspector::GetRespawnRequest() const {
	return respawnRequest_;
}

SceneObjectId GameplayInspector::GetJumpFrameRequest() const {
	return jumpFrameRequest_;
}
