#include "panels/PlayStatePanel.h"

#include "gameplay/EditorPlaySession.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include <format>
#include <imgui.h>

void PlayStatePanel::Show() {
	open_ = true;
	focusRequested_ = true;
}

void PlayStatePanel::Reset() {
	open_ = false;
	focusRequested_ = false;
}

PlayStatePanel::Request PlayStatePanel::Draw(
    const ImGuiSystem &ui, const EditorPlaySession &session,
    const std::filesystem::path &initialScenePath) {
	if (!open_) {
		return {};
	}

	ImGui::SetNextWindowSize({400.0f, 440.0f}, ImGuiCond_FirstUseEver);

	if (focusRequested_) {
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
		focusRequested_ = false;
	}

	if (!ImGui::Begin("Play State", &open_)) {
		ImGui::End();
		return {};
	}

	if (!session.IsPlaying()) {
		ui.Text("Stopped");
		ImGui::End();
		return {};
	}

	const auto *loaded = session.GetLoadedScenePath();
	const auto &scenePath = loaded ? *loaded : initialScenePath;
	const auto sceneName = scenePath.empty() ? "Untitled Scene"
	                                        : PathUtils::ToUtf8(scenePath.filename());
	const char *status = session.IsPaused()       ? "Editor Paused"
	                     : session.IsGamePaused() ? "Game Paused"
	                                              : "Running";
	const auto time =
	    std::format("Simulated Time: {:.3f} s", session.GetElapsedSeconds());
	const auto frames =
	    std::format("Simulation Frames: {}", session.GetSimulationFrameCount());
	const auto messages = std::format("Queued Script Messages: {}",
	                                  session.GetPendingScriptMessages());
	ui.Text("Scene: " + sceneName);
	if (!scenePath.empty()) {
		ImGui::SetItemTooltip("%s", PathUtils::ToUtf8(scenePath).c_str());
	}
	ui.Text(status);
	ui.Text(time);
	ui.Text(frames);
	ui.Text(messages);

	auto data = session.GetSharedScriptData();
	if (data.empty())
		data = "{}";
	if (ui.Button("Copy Summary")) {
		const auto text = std::format(
		    "Scene: {}\n{}\n{}\n{}\n{}\n"
		    "Shared Script Data:\n{}",
		    scenePath.empty() ? sceneName : PathUtils::ToUtf8(scenePath),
		    status, time, frames, messages,
		    data);
		ImGui::SetClipboardText(text.c_str());
	}
	if (ImGui::CollapsingHeader("Shared Script Data")) {
		ImGui::TextWrapped("%.*s", static_cast<int>(data.size()), data.data());
	}

	Request requested;
	const auto select = [&](SceneObjectId id, const char *selectLabel,
	                        const char *frameLabel) {
		ui.BeginDisabled(id == 0);

		if (ui.Button(selectLabel) && id != 0) {
			requested = {id, false};
		}

		ui.SameLine();

		if (ui.Button(frameLabel) && id != 0) {
			requested = {id, true};
		}

		ui.EndDisabled();
	};

	const auto vector = [&ui](const char *label,
	                          const DirectX::XMFLOAT3 &value) {
		ui.Text(std::format("{}: {:.3f}, {:.3f}, {:.3f}", label, value.x,
		                    value.y, value.z));
	};

	ui.Separator();

	if (const auto character = session.GetCharacterState()) {
		const auto *object = session.GetScene().FindObject(character->objectId);
		ui.Text(std::format("Character: {} (ID {})", object ? object->name : "",
		                    character->objectId));
		vector("Position", character->position);
		ui.Text(std::format("Vertical Velocity: {:.3f}",
		                    character->verticalVelocity));
		ui.Text(character->grounded ? "Grounded: Yes" : "Grounded: No");
		ui.Text(std::format("Respawns: {}", character->respawnCount));

		if (character->lastRespawnPosition) {
			vector("Last Respawn", *character->lastRespawnPosition);
		} else {
			ui.Text("Last Respawn: None");
		}

		select(object ? character->objectId : 0, "Select Character",
		       "Frame Character");

		const auto *ground =
		    session.GetScene().FindObject(character->groundObjectId);
		const bool hasGround = ground && ground->collider;

		if (hasGround) {
			ui.Text(std::format("Ground Collider: {} (ID {})",
			                    ground->name.empty() ? "Object" : ground->name,
			                    character->groundObjectId));
		} else {
			ui.Text("Ground Collider: None");
		}

		select(hasGround ? character->groundObjectId : 0,
		       "Select Ground Collider", "Frame Ground Collider");
	} else {
		ui.Text("Character: None");
	}

	ui.Separator();
	const auto cameraId = session.GetCameraObjectId();
	const auto *camera = session.GetScene().FindObject(cameraId);

	if (camera && camera->camera) {
		ui.Text(std::format("Camera: {} (ID {})",
		                    camera->name.empty() ? "Object" : camera->name,
		                    cameraId));
	} else {
		ui.Text("Camera: Fixed View");
	}

	select(camera && camera->camera ? cameraId : 0, "Select Camera",
	       "Frame Camera");
	const auto &pose = session.GetCameraPose();
	const auto &lens = session.GetCameraSettings();
	vector("Position", pose.position);
	vector("Target", pose.target);
	vector("Up", pose.up);
	ui.Text(std::format("Vertical FOV: {:.3f}", lens.verticalFovDegrees));
	ui.Text(std::format("Near / Far: {:.3f} / {:.3f}", lens.nearClip,
	                    lens.farClip));
	ui.Separator();

	if (ui.CollapsingHeader("Triggers")) {
		const auto selectObject = [&](SceneObjectId id) {
			const auto *object = session.GetScene().FindObject(id);
			const auto label = std::format(
			    "{} (ID {})", object ? object->name : "Missing", id);
			ui.BeginDisabled(!object);

			if (ImGui::Selectable(label.c_str(), false,
			                      ImGuiSelectableFlags_AllowDoubleClick)) {
				requested = {
				    id, ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)};
			}

			ui.EndDisabled();
		};
		const auto contacts = session.GetTriggerContacts();
		ui.Text(std::format("Active Contacts: {}", contacts.size()));
		const auto flags = ImGuiTableFlags_RowBg |
		                   ImGuiTableFlags_BordersInnerV |
		                   ImGuiTableFlags_ScrollY;
		if (!contacts.empty() &&
		    ImGui::BeginTable("TriggerContacts", 2, flags, {0, 120})) {
			ImGui::TableSetupColumn("First Collider");
			ImGui::TableSetupColumn("Second Collider");
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			for (std::size_t i = 0; i < contacts.size(); ++i) {
				ImGui::PushID(static_cast<int>(i));
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				selectObject(contacts[i].first);
				ImGui::TableNextColumn();
				selectObject(contacts[i].second);
				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		const auto records = session.GetTriggerHistory();
		ui.Text(std::format("Recent Enter / Exit: {}", records.size()));

		if (!records.empty() &&
		    ImGui::BeginTable("TriggerHistory", 4, flags, {0, 180})) {
			ImGui::TableSetupColumn("Event");
			ImGui::TableSetupColumn("Frame / Time");
			ImGui::TableSetupColumn("First Collider");
			ImGui::TableSetupColumn("Second Collider");
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			for (std::size_t i = records.size(); i > 0; --i) {
				const auto &record = records[i - 1];
				ImGui::PushID(static_cast<int>(i));
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ui.Text(record.event.type == TriggerEvent::Type::ENTER
				            ? "Enter"
				            : "Exit");
				ImGui::TableNextColumn();
				ui.Text(
				    std::format("{} / {:.3f} s", record.frame, record.seconds));
				ImGui::TableNextColumn();
				selectObject(record.event.contact.first);
				ImGui::TableNextColumn();
				selectObject(record.event.contact.second);
				ImGui::PopID();
			}

			ImGui::EndTable();
		}
	}

	ImGui::End();
	return requested;
}
