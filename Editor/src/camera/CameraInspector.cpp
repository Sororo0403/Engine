#include "camera/CameraInspector.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "scene/SceneObject.h"
#include <imgui.h>
#include <string>

bool CameraInspector::Draw(const ImGuiSystem &ui, SceneObjectId id,
                           SceneObject &object, EditorHistory &history,
                           Request &request, bool canPasteLens) const {
	if (!object.camera) {
		return false;
	}

	const auto scope = std::to_string(id);
	ImGui::PushID(scope.c_str());
	ImGui::PushID("CameraInspector");
	bool changed = false;

	if (ui.CollapsingHeader("Camera")) {
		auto edited = *object.camera;

		if (ImGui::Checkbox("Primary", &edited.primary)) {
			if (edited.primary) {
				request = {Action::MAKE_PRIMARY, id};
			} else {
				changed |= history.SetCamera(id, edited);
			}
		}

		auto field = [&](const char *label, float CameraSettings::*member,
		                 float speed, float minimum, float maximum) {
			auto value = *object.camera;
			const bool edited = ImGui::DragFloat(
			    label, &(value.settings.*member), speed, minimum, maximum,
			    "%.3f", ImGuiSliderFlags_AlwaysClamp);

			if (ImGui::IsItemActivated()) {
				history.BeginCamera(id);
			}

			if (edited && value.IsValid() && value != *object.camera) {
				if (history.IsEditing() || history.BeginCamera(id)) {
					object.camera = value;
					changed = true;
				}
			}

			if (ImGui::IsItemDeactivated() && !ImGui::IsAnyItemActive()) {
				history.CommitEdit();
			}
		};
		field("Vertical FOV", &CameraSettings::verticalFovDegrees, 0.1f, 1,
		      179);
		field("Near Clip", &CameraSettings::nearClip, 0.001f, 0.001f, 100000);
		field("Far Clip", &CameraSettings::farClip, 0.1f, 0.002f, 100000);

		ui.BeginDisabled(!object.camera->settings.IsValid());

		if (ui.Button("Copy Lens Settings")) {
			request = {Action::COPY_LENS, id};
		}

		ui.EndDisabled();
		ui.SameLine();
		ui.BeginDisabled(!canPasteLens);

		if (ui.Button("Paste Lens Settings")) {
			request = {Action::PASTE_LENS, id};
		}

		ui.EndDisabled();

		if (ui.Button("Reset Lens Settings")) {
			const auto before = object.camera;
			changed |= history.ResetComponentSettings(
			               id, EditorHistory::ComponentSettings::CAMERA_LENS) &&
			           object.camera != before;
		}

		if (ui.Button("Align to Scene View")) {
			request = {Action::ALIGN_VIEW, id};
		}

		if (ui.Button("Remove Camera")) {
			changed |= history.SetCamera(id, std::nullopt);
		}
	}

	ImGui::PopID();
	ImGui::PopID();
	return changed;
}
