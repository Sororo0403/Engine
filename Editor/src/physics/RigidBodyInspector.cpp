#include "physics/RigidBodyInspector.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "scene/SceneObject.h"
#include <format>
#include <imgui.h>
#include <string>

bool RigidBodyInspector::Draw(const ImGuiSystem &ui, SceneObjectId id,
                              SceneObject &object,
                              EditorHistory &history) const {
	if (!object.rigidBody)
		return false;
	const bool open = ui.CollapsingHeader("Rigid Body");
	ImGui::SetItemTooltip("Right-click for rigid body actions.");
	if (ImGui::BeginPopupContextItem("RigidBodyActions")) {
		const bool remove = ui.MenuItem("Remove Rigid Body");
		ImGui::EndPopup();
		if (remove)
			return history.SetRigidBody(id, std::nullopt);
	}
	if (!open)
		return false;
	const auto scope = std::to_string(id);
	ImGui::PushID(scope.c_str());
	ImGui::PushID("RigidBodyInspector");
	auto value = *object.rigidBody;
	bool changed = false;
	const auto flag = [&](bool edited) {
		if (edited)
			changed |= history.SetRigidBody(id, value);
	};
	const auto apply = [&](bool edited) {
		const bool ready =
		    !ImGui::IsItemActivated() || history.BeginRigidBody(id);
		if (ready && edited && value.IsValid() && value != *object.rigidBody &&
		    (history.IsEditing() || history.BeginRigidBody(id))) {
			object.rigidBody = value;
			changed = true;
		}

		if (ImGui::IsItemDeactivated() && !ImGui::IsAnyItemActive())
			history.CommitEdit();
	};
	flag(ui.Checkbox("Rigid Body Enabled", value.enabled));
	flag(ui.Checkbox("Is Kinematic", value.isKinematic));
	apply(ImGui::DragFloat(
	    "Mass (kg)", &value.mass, .05f, RigidBodySettings::kMinimumMass,
	    RigidBodySettings::kMaximumMass, "%.3f", ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::SliderFloat("Friction", &value.friction, 0, 1));
	apply(ImGui::SliderFloat("Restitution", &value.restitution, 0, 1));
	apply(ImGui::DragFloat("Gravity Factor", &value.gravityFactor, .02f, 0, 10,
	                       "%.3f", ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::SliderFloat("Linear Damping", &value.linearDamping, 0, 1));
	apply(ImGui::SliderFloat("Angular Damping", &value.angularDamping, 0, 1));
	apply(ImGui::DragFloat3("Initial Velocity", &value.linearVelocity.x, .05f,
	                        -RigidBodySettings::kMaximumLinearSpeed,
	                        RigidBodySettings::kMaximumLinearSpeed, "%.3f",
	                        ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::DragFloat3("Initial Angular Velocity (rad/s)",
	                        &value.angularVelocity.x, .02f,
	                        -RigidBodySettings::kMaximumAngularSpeed,
	                        RigidBodySettings::kMaximumAngularSpeed, "%.3f",
	                        ImGuiSliderFlags_AlwaysClamp));
	flag(ui.Checkbox("Freeze Rotation X", value.freezeRotation[0]));
	flag(ui.Checkbox("Freeze Rotation Y", value.freezeRotation[1]));
	flag(ui.Checkbox("Freeze Rotation Z", value.freezeRotation[2]));
	ImGui::PopID();
	ImGui::PopID();
	return changed;
}

void RigidBodyInspector::DrawReadOnly(const ImGuiSystem &ui,
                                      const SceneObject &object) const {
	if (!object.rigidBody || !ui.CollapsingHeader("Rigid Body"))
		return;
	const auto &r = *object.rigidBody;
	ui.Text(
	    std::format("Enabled: {} / Kinematic: {}", r.enabled, r.isKinematic));
	ui.Text(std::format("Mass: {:g} kg / Friction: {:g} / Restitution: {:g}",
	                    r.mass, r.friction, r.restitution));
	ui.Text(std::format("Gravity Factor: {:g} / Damping: {:g}, {:g}",
	                    r.gravityFactor, r.linearDamping, r.angularDamping));
	ui.Text(std::format("Initial Velocity: {:g}, {:g}, {:g}",
	                    r.linearVelocity.x, r.linearVelocity.y,
	                    r.linearVelocity.z));
	ui.Text(std::format("Initial Angular Velocity: {:g}, {:g}, {:g}",
	                    r.angularVelocity.x, r.angularVelocity.y,
	                    r.angularVelocity.z));
	ui.Text(std::format("Freeze Rotation: X {} / Y {} / Z {}",
	                    r.freezeRotation[0], r.freezeRotation[1],
	                    r.freezeRotation[2]));
}
