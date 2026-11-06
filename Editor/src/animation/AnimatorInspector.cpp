#include "animation/AnimatorInspector.h"
#include "animation/ModelAnimationData.h"
#include "graphics/model/ModelManager.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "scene/SceneObject.h"
#include <algorithm>
#include <format>
#include <imgui.h>

bool AnimatorInspector::Draw(const ImGuiSystem &ui, SceneObjectId id,
                             SceneObject &object, EditorHistory &history,
                             ModelManager *models) const {
	if (!object.animator || !ui.CollapsingHeader("Animator"))
		return false;
	const auto scope = std::to_string(id);
	ImGui::PushID(scope.c_str());
	ImGui::PushID("AnimatorInspector");
	bool changed = false;
	auto edited = *object.animator;
	auto data = models && object.HasMesh()
	                ? models->GetAnimationData(
	                      models->FindLoaded(object.mesh->modelPath))
	                : nullptr;
	const auto label =
	    edited.clip.empty() ? std::string("First Clip") : edited.clip;
	if (ImGui::BeginCombo("Default Clip", label.c_str())) {
		if (!data && models && object.HasMesh() && ImGui::IsWindowAppearing())
			data =
			    models->GetAnimationData(models->Load(object.mesh->modelPath));
		if (ui.Selectable("First Clip", edited.clip.empty())) {
			edited.clip.clear();
			changed |= history.SetAnimator(id, edited);
		}
		if (data)
			for (std::size_t i = 0; i < data->clips.size(); ++i) {
				ImGui::PushID(static_cast<int>(i));
				if (ui.Selectable(data->clips[i].name.c_str(),
				                  edited.clip == data->clips[i].name)) {
					edited.clip = data->clips[i].name;
					changed |= history.SetAnimator(id, edited);
				}
				ImGui::PopID();
			}
		else
			ui.Text("No animation clips");
		ImGui::EndCombo();
	}
	const bool speedChanged = ImGui::DragFloat(
	    "Animator Speed", &edited.speed, .05f, .01f,
	    AnimatorComponent::kMaximumSpeed, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	const bool begin = !ImGui::IsItemActivated() || history.BeginAnimator(id);
	if (begin && speedChanged && edited.IsValid() &&
	    edited != *object.animator &&
	    (history.IsEditing() || history.BeginAnimator(id))) {
		object.animator = edited;
		changed = true;
	}
	if (ImGui::IsItemDeactivated() && !ImGui::IsAnyItemActive())
		history.CommitEdit();
	if (ImGui::Checkbox("Loop Clip", &edited.loop))
		changed |= history.SetAnimator(id, edited);
	if (ImGui::Checkbox("Play Clip On Start", &edited.playOnStart))
		changed |= history.SetAnimator(id, edited);
	if (data && !data->clips.empty()) {
		const auto at =
		    edited.clip.empty()
		        ? data->clips.begin()
		        : std::find_if(data->clips.begin(), data->clips.end(),
		                       [&](const auto &clip) {
			                       return clip.name == edited.clip;
		                       });
		if (at != data->clips.end())
			ui.Text(std::format("Clip Duration: {:.2f} s", at->duration));
	}
	if (ui.Button("Reset Animator"))
		changed |= history.SetAnimator(id, AnimatorComponent{});
	if (ui.Button("Remove Animator"))
		changed |= history.SetAnimator(id, std::nullopt);
	ImGui::PopID();
	ImGui::PopID();
	return changed;
}

void AnimatorInspector::DrawReadOnly(const ImGuiSystem &ui,
                                     const SceneObject &object) const {
	if (!object.animator || !ui.CollapsingHeader("Animator"))
		return;
	const auto &a = *object.animator;
	ui.Text("Default Clip: " +
	        (a.clip.empty() ? std::string("First Clip") : a.clip));
	ui.Text(std::format("Initial Speed: {:.2f}", a.speed));
	ui.Text(a.loop ? "Initial Loop: On" : "Initial Loop: Off");
	ui.Text(a.playOnStart ? "Play On Start: On" : "Play On Start: Off");
}
