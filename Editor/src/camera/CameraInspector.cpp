#include "camera/CameraInspector.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "scene/SceneObject.h"
#include <imgui.h>
#include <string>

bool CameraInspector::Draw(const ImGuiSystem &ui, SceneObjectId id,
                           SceneObject &object, EditorHistory &history,
                           Request &request) const {
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

		if (ui.Button("Reset Lens Settings")) {
			history.CommitEdit();
			const CameraSettings defaults;

			// Primaryを切り替えず、レンズだけの変更を独立した履歴にする。
			if (object.camera->settings != defaults &&
			    history.BeginCamera(id)) {
				object.camera->settings = defaults;
				history.CommitEdit();
				changed = true;
			}
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
