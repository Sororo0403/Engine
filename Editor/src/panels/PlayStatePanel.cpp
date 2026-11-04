#include "panels/PlayStatePanel.h"

#include "gameplay/EditorPlaySession.h"
#include "graphics/ui/ImGuiSystem.h"
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

PlayStatePanel::Request PlayStatePanel::Draw(const ImGuiSystem &ui,
                                             const EditorPlaySession &session) {
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

	ui.Text(!session.IsPlaying() ? "Stopped"
	        : session.IsPaused() ? "Paused"
	                             : "Running");

	if (!session.IsPlaying()) {
		ImGui::End();
		return {};
	}

	ui.Text(
	    std::format("Simulated Time: {:.3f} s", session.GetElapsedSeconds()));
	ui.Text(std::format("Simulation Frames: {}",
	                    session.GetSimulationFrameCount()));

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
	ImGui::End();
	return requested;
}
