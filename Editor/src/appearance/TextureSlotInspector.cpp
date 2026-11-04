#include "appearance/TextureSlotInspector.h"
#include "assets/AssetDatabase.h"
#include "graphics/model/ModelManager.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "platform/windows/PathUtils.h"
#include <algorithm>
#include <format>
#include <imgui.h>
#include <utility>
#include <vector>

bool TextureSlotInspector::Draw(const ImGuiSystem &ui, SceneObjectId id,
                                SceneObject &object, EditorHistory &history,
                                ModelManager &models,
                                const AssetDatabase &assets, Request &request,
                                bool canApplyUvToSelection,
                                bool canPasteUv) const {
	request = {};
	if (!object.meshRenderer) {
		return false;
	}

	bool changed = false;
	ImGui::PushID(std::to_string(id).c_str());
	ImGui::PushID("TextureSlots");

	const auto editUv = [&](const char *label, bool scale) {
		auto edited = *object.meshRenderer;
		auto &values = scale ? edited.uvScale : edited.uvOffset;
		const bool valueChanged =
		    ImGui::DragFloat2(label, values.data(), 0.01f);
		const bool begin =
		    !ImGui::IsItemActivated() || history.BeginMeshRenderer(id);

		if (begin && valueChanged && edited.IsValid() &&
		    edited != *object.meshRenderer &&
		    (history.IsEditing() || history.BeginMeshRenderer(id))) {
			object.meshRenderer = std::move(edited);
			changed = true;
		}

		if (ImGui::IsItemDeactivated() && !ImGui::IsAnyItemActive()) {
			history.CommitEdit();
		}
	};

	editUv("UV Tiling", true);
	editUv("UV Offset", false);

	if (ui.Button("Reset UV")) {
		const auto before = object.meshRenderer;
		auto reset = *object.meshRenderer;
		reset.uvScale = {1.0f, 1.0f};
		reset.uvOffset = {0.0f, 0.0f};
		changed |=
		    history.SetMeshRenderer(id, reset) && object.meshRenderer != before;
	}

	ui.BeginDisabled(!object.meshRenderer->IsValid());

	if (ui.Button("Copy UV") && object.meshRenderer->IsValid()) {
		request.copyUv = true;
	}

	ui.EndDisabled();
	ui.BeginDisabled(!canPasteUv || !canApplyUvToSelection);

	if (ui.Button("Paste UV to Selected Objects") && canPasteUv &&
	    canApplyUvToSelection) {
		request.pasteUvToSelection = true;
	}

	ui.EndDisabled();
	ui.BeginDisabled(!canApplyUvToSelection);

	if (ui.Button("Apply UV to Selected Objects") && canApplyUvToSelection) {
		request.applyUvToSelection = true;
	}

	ui.EndDisabled();

	{
		const auto model = !object.HasMesh()
		                       ? ModelHandle{}
		                       : models.FindLoaded(object.mesh->modelPath);
		const auto count = models.GetMaterialCount(model);

		for (std::size_t slot = 0; slot < count; ++slot) {
			ImGui::PushID(static_cast<int>(slot));
			const auto found = std::find_if(
			    object.meshRenderer->textures.slots.begin(),
			    object.meshRenderer->textures.slots.end(),
			    [slot](const auto &binding) { return binding.first == slot; });
			const auto preview =
			    found == object.meshRenderer->textures.slots.end()
			        ? std::string("White (No Texture)")
			        : PathUtils::ToUtf8(found->second);
			const auto label = std::format("Slot {}", slot);

			if (ImGui::BeginCombo(label.c_str(), preview.c_str())) {
				if (ui.Selectable(
				        "White (No Texture)",
				        found == object.meshRenderer->textures.slots.end())) {
					changed |= history.SetTexturePath(
					    id, static_cast<std::uint32_t>(slot), {});
				}

				for (const auto &path : assets.GetTextures()) {
					const auto current = std::find_if(
					    object.meshRenderer->textures.slots.begin(),
					    object.meshRenderer->textures.slots.end(),
					    [slot](const auto &binding) {
						    return binding.first == slot;
					    });
					const auto name = PathUtils::ToUtf8(path);

					if (ui.Selectable(
					        name.c_str(),
					        current !=
					                object.meshRenderer->textures.slots.end() &&
					            current->second == path)) {
						changed |= history.SetTexturePath(
						    id, static_cast<std::uint32_t>(slot), path);
					}
				}

				ImGui::EndCombo();
			}

			const auto assigned = std::find_if(
			    object.meshRenderer->textures.slots.begin(),
			    object.meshRenderer->textures.slots.end(),
			    [slot](const auto &binding) { return binding.first == slot; });
			ui.BeginDisabled(assigned ==
			                 object.meshRenderer->textures.slots.end());

			if (ui.Button("Show Texture in Assets")) {
				request.revealTexture = assigned->second;
			}

			ui.EndDisabled();
			ImGui::PopID();
		}

		// モデルの差し替えで使用されなくなった割り当ても、勝手に破棄しない。
		std::vector<std::uint32_t> unused;

		for (const auto &[slot, path] : object.meshRenderer->textures.slots) {
			if (slot >= count) {
				unused.push_back(slot);
			}
		}

		for (const auto slot : unused) {
			ImGui::PushID(std::format("unused{}", slot).c_str());
			ui.Text(std::format(
			    "Unused slot {}: {}", slot,
			    PathUtils::ToUtf8(
			        std::find_if(object.meshRenderer->textures.slots.begin(),
			                     object.meshRenderer->textures.slots.end(),
			                     [slot](const auto &binding) {
				                     return binding.first == slot;
			                     })
			            ->second)));

			if (ui.Button("Show Texture in Assets")) {
				request.revealTexture =
				    std::find_if(object.meshRenderer->textures.slots.begin(),
				                 object.meshRenderer->textures.slots.end(),
				                 [slot](const auto &binding) {
					                 return binding.first == slot;
				                 })
				        ->second;
			}

			if (ui.Button("Clear Assignment")) {
				changed |= history.SetTexturePath(id, slot, {});
			}

			ImGui::PopID();
		}
	}

	ImGui::PopID();
	ImGui::PopID();
	return changed;
}
