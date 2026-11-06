#include "particles/ParticleEmitterInspector.h"
#include "assets/AssetDragDrop.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "platform/windows/PathUtils.h"
#include "scene/SceneObject.h"
#include <format>
#include <imgui.h>

bool ParticleEmitterInspector::Draw(
    const ImGuiSystem &ui, SceneObjectId id, SceneObject &object,
    EditorHistory &history,
    std::span<const std::filesystem::path> textures) const {
	if (!object.particleEmitter || !ui.CollapsingHeader("Particle Emitter"))
		return false;
	const auto scope = std::to_string(id);
	ImGui::PushID(scope.c_str());
	ImGui::PushID("ParticleEmitterInspector");
	bool changed = false;
	auto p = *object.particleEmitter;
	const auto apply = [&](bool valueChanged) {
		const bool begin =
		    !ImGui::IsItemActivated() || history.BeginParticleEmitter(id);
		if (begin && valueChanged && p.IsValid() &&
		    p != *object.particleEmitter &&
		    (history.IsEditing() || history.BeginParticleEmitter(id))) {
			object.particleEmitter = p;
			changed = true;
		}
		if (ImGui::IsItemDeactivated() && !ImGui::IsAnyItemActive())
			history.CommitEdit();
	};
	const auto label = p.texture.empty()
	                       ? std::string("Soft Circle")
	                       : PathUtils::ToUtf8(p.texture.filename());
	if (ImGui::BeginCombo("Particle Image", label.c_str())) {
		if (ui.Selectable("Soft Circle", p.texture.empty())) {
			p.texture.clear();
			changed |= history.SetParticleEmitter(id, p);
		}
		for (const auto &path : textures)
			if (ui.Selectable(PathUtils::ToUtf8(path.filename()).c_str(),
			                  !p.texture.empty() &&
			                      PathUtils::IsSameFile(path, p.texture))) {
				p.texture = path;
				changed |= history.SetParticleEmitter(id, p);
			}
		ImGui::EndCombo();
	}
	const auto dropped =
	    AssetDragDrop::AcceptTarget(AssetDragDrop::Kind::TEXTURE);
	if (!dropped.empty()) {
		p.texture = dropped;
		changed |= history.SetParticleEmitter(id, p);
	}
	if (ImGui::Checkbox("Play On Start", &p.playOnStart))
		changed |= history.SetParticleEmitter(id, p);
	if (ImGui::Checkbox("Loop", &p.loop))
		changed |= history.SetParticleEmitter(id, p);
	if (ImGui::Checkbox("Local Space", &p.localSpace))
		changed |= history.SetParticleEmitter(id, p);
	if (ImGui::Checkbox("Additive", &p.additive))
		changed |= history.SetParticleEmitter(id, p);
	apply(ImGui::InputScalar("Seed", ImGuiDataType_U32, &p.seed));
	apply(ImGui::DragInt("Max Particles", &p.maxParticles, 1, 1,
	                     ParticleEmitterComponent::kMaximumParticles, "%d",
	                     ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::DragInt("Start Burst", &p.burst, 1, 0,
	                     ParticleEmitterComponent::kMaximumParticles, "%d",
	                     ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::DragFloat("Emission Rate", &p.rate, 1, 0,
	                       ParticleEmitterComponent::kMaximumRate, "%.1f",
	                       ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::DragFloat("Duration", &p.duration, .05f, .05f, 3600, "%.2f",
	                       ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::DragFloat("Lifetime Min", &p.lifetimeMin, .01f, .01f,
	                       p.lifetimeMax, "%.2f",
	                       ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::DragFloat("Lifetime Max", &p.lifetimeMax, .01f, p.lifetimeMin,
	                       ParticleEmitterComponent::kMaximumLifetime, "%.2f",
	                       ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::DragFloat("Speed Min", &p.speedMin, .05f, 0, p.speedMax,
	                       "%.2f", ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::DragFloat("Speed Max", &p.speedMax, .05f, p.speedMin, 1000,
	                       "%.2f", ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::DragFloat("Emission Radius", &p.radius, .01f, 0, 1000, "%.2f",
	                       ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::DragFloat("Cone Angle", &p.coneDegrees, .5f, 0, 180, "%.1f",
	                       ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::DragFloat("Start Size", &p.sizeStart, .01f, 0, 1000, "%.2f",
	                       ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::DragFloat("End Size", &p.sizeEnd, .01f, 0, 1000, "%.2f",
	                       ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::DragFloat("Rotation Speed", &p.rotationSpeed, 1, -720, 720,
	                       "%.1f", ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::DragFloat3("Gravity", &p.gravity.x, .05f, -1000, 1000, "%.2f",
	                        ImGuiSliderFlags_AlwaysClamp));
	apply(ImGui::ColorEdit4("Start Color", p.colorStart.data()));
	apply(ImGui::ColorEdit4("End Color", p.colorEnd.data()));
	if (ui.Button("Reset Particle Emitter"))
		changed |= history.SetParticleEmitter(id, ParticleEmitterComponent{});
	if (ui.Button("Remove Particle Emitter"))
		changed |= history.SetParticleEmitter(id, std::nullopt);
	ImGui::PopID();
	ImGui::PopID();
	return changed;
}

void ParticleEmitterInspector::DrawReadOnly(const ImGuiSystem &ui,
                                            const SceneObject &object) const {
	if (!object.particleEmitter || !ui.CollapsingHeader("Particle Emitter"))
		return;
	const auto &p = *object.particleEmitter;
	ui.Text(std::format("Max Particles: {}", p.maxParticles));
	ui.Text(std::format("Emission Rate: {:.1f}", p.rate));
	ui.Text(p.localSpace ? "Space: Local" : "Space: World");
	ui.Text(p.additive ? "Blend: Additive" : "Blend: Alpha");
}
