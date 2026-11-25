#include "appearance/TextureSlotInspector.h"
#include "assets/AssetDatabase.h"
#include "graphics/model/ModelManager.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "platform/windows/PathUtils.h"
#include <algorithm>
#include <format>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <stdexcept>
#include <utility>
#include <vector>

bool TextureSlotInspector::DrawMaterialSlot(SceneObjectId id, std::uint32_t slot,
                                            SceneObject &object, EditorHistory &history,
                                            AssetDatabase &assets) {
	const auto &slots = object.meshRenderer->materials.slots;
	const auto found = std::ranges::lower_bound(slots, slot, {}, [](const auto &binding) { return binding.first; });
	const auto assigned = found != slots.end() && found->first == slot
	                          ? found->second : std::filesystem::path{};
	const auto assign = [&](const std::filesystem::path &path) {
		if (path == assigned)
			return false;
		auto edited = *object.meshRenderer;
		auto &bindings = edited.materials.slots;
		const auto index = std::ranges::lower_bound(
		    bindings, slot, {}, [](const auto &binding) { return binding.first; });
		if (index != bindings.end() && index->first == slot) {
			if (path.empty())
				bindings.erase(index);
			else
				index->second = path;
		} else if (!path.empty()) {
			bindings.insert(index, {slot, path});
		}
		return history.SetMeshRenderer(id, edited);
	};

	bool changed = false;
	const auto label = assigned.empty() ? "Model Default" : PathUtils::ToUtf8(assigned);
	if (ImGui::BeginCombo("Material Asset", label.c_str())) {
		if (ImGui::Selectable("Model Default", assigned.empty()))
			changed |= assign({});
		for (const auto &path : assets.GetMaterials()) {
			const auto name = PathUtils::ToUtf8(path);
			if (ImGui::Selectable(name.c_str(), path == assigned))
				changed |= assign(path);
		}
		ImGui::EndCombo();
	}

	bool create = false;
	bool edit = false;
	if (ImGui::SmallButton("Material Actions"))
		ImGui::OpenPopup("MaterialActions");
	if (ImGui::BeginPopup("MaterialActions")) {
		create = ImGui::MenuItem("New Material...", nullptr, false, !assets.GetRootDirectory().empty());
		edit = ImGui::MenuItem("Edit Material...", nullptr, false, !assigned.empty());
		ImGui::EndPopup();
	}
	if (create) {
		editingMaterial_.clear();
		materialName_ = "New Material";
		materialDraft_ = {};
		materialError_.clear();
		ImGui::OpenPopup("Material");
	}
	if (edit) {
		editingMaterial_ = assigned;
		if (MaterialSerializer::Load(assigned, materialDraft_, materialError_))
			ImGui::OpenPopup("Material");
		else
			ImGui::OpenPopup("Material Load Error");
	}
	if (ImGui::BeginPopupModal("Material Load Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextWrapped("%s", materialError_.c_str());
		if (ImGui::Button("Close"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	if (!ImGui::BeginPopupModal("Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return changed;

	const bool creating = editingMaterial_.empty();
	if (creating)
		ImGui::InputText("Name", &materialName_);
	else
		ImGui::TextUnformatted(PathUtils::ToUtf8(editingMaterial_).c_str());
	auto &data = materialDraft_.data;
	float color[]{data.baseColor.x, data.baseColor.y, data.baseColor.z, data.baseColor.w};
	if (ImGui::ColorEdit4("Base Color", color))
		data.baseColor = {color[0], color[1], color[2], color[3]};
	float scale[]{data.uvScale.x, data.uvScale.y};
	if (ImGui::DragFloat2("Tiling", scale, .01f))
		data.uvScale = {scale[0], scale[1]};
	float offset[]{data.uvOffset.x, data.uvOffset.y};
	if (ImGui::DragFloat2("Offset", offset, .01f))
		data.uvOffset = {offset[0], offset[1]};
	bool unlit = data.shadingMode == MaterialData::ShadingMode::UNLIT;
	if (ImGui::Checkbox("Unlit", &unlit))
		data.shadingMode = unlit ? MaterialData::ShadingMode::UNLIT : MaterialData::ShadingMode::LIT;

	const auto materialDirectory = creating
	    ? PathUtils::ResolveFromExecutable(assets.GetRootDirectory())
	    : PathUtils::ResolveFromExecutable(editingMaterial_).parent_path();
	const auto &texture = materialDraft_.baseColorTexture;
	const auto textureLabel = texture.empty() ? "White" : PathUtils::ToUtf8(texture);
	if (ImGui::BeginCombo("Base Color Texture", textureLabel.c_str())) {
		if (ImGui::Selectable("White", texture.empty()))
			materialDraft_.baseColorTexture.clear();
		for (const auto &path : assets.GetTextures()) {
			const auto relative = PathUtils::ResolveFromExecutable(path).lexically_relative(materialDirectory);
			const auto name = PathUtils::ToUtf8(path);
			if (ImGui::Selectable(name.c_str(), relative == texture, relative.empty() ? ImGuiSelectableFlags_Disabled : 0))
				materialDraft_.baseColorTexture = relative;
		}
		ImGui::EndCombo();
	}

	if (ImGui::Button(creating ? "Create and Assign" : "Save Shared Material")) {
		try {
			auto path = editingMaterial_;
			if (creating) {
				auto name = PathUtils::FromUtf8(materialName_);
				if (!MaterialSerializer::IsPath(name))
					name += L".material.json";
				if (materialName_.empty() || !PathUtils::IsValidFileName(name))
					throw std::invalid_argument("Enter a valid material name.");
				path = materialDirectory / name;
			}
			if (MaterialSerializer::Save(path, materialDraft_, materialError_, !creating)) {
				if (creating) {
					changed |= assign(path);
					assets.Refresh();
				}
				ImGui::CloseCurrentPopup();
			}
		} catch (const std::exception &error) {
			materialError_ = error.what();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel"))
		ImGui::CloseCurrentPopup();
	if (!materialError_.empty())
		ImGui::TextWrapped("%s", materialError_.c_str());
	ImGui::EndPopup();
	return changed;
}

bool TextureSlotInspector::Draw(const ImGuiSystem &ui, SceneObjectId id,
                                SceneObject &object, EditorHistory &history,
                                ModelManager &models,
                                AssetDatabase &assets, Request &request,
                                bool canApplyUvToSelection,
                                bool canPasteUv) {
	request = {};

	if (!object.meshRenderer) {
		return false;
	}

	bool changed = false;
	ImGui::PushID(std::to_string(id).c_str());
	ImGui::PushID("TextureSlots");

	const auto editRenderer = [&](const auto &draw) {
		auto edited = *object.meshRenderer;
		const bool valueChanged = draw(edited);
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

	if (ImGui::TreeNode("UV and Effects")) {
		editRenderer([](auto &renderer) {
			return ImGui::DragFloat2("UV Tiling", renderer.uvScale.data(), .01f);
		});
		editRenderer([](auto &renderer) {
			return ImGui::DragFloat2("UV Offset", renderer.uvOffset.data(), .01f);
		});
		if (ImGui::TreeNode("Radial Reveal")) {
			editRenderer([](auto &renderer) {
				return ImGui::DragFloat3("World Origin",
				                        renderer.reveal.originRadius.data(), .1f);
			});
			editRenderer([](auto &renderer) {
				return ImGui::DragFloat("Radius", &renderer.reveal.originRadius[3], .1f);
			});
			editRenderer([](auto &renderer) {
				return ImGui::DragFloat("Edge Width (0 = Off)", &renderer.reveal.feather,
				                       .01f, 0.f, 100000.f, "%.2f",
				                       ImGuiSliderFlags_AlwaysClamp);
			});
			ImGui::TreePop();
		}

		if (ui.Button("UV Actions"))
			ImGui::OpenPopup("UVActions");
		if (ImGui::BeginPopup("UVActions")) {
			if (ui.MenuItem("Reset UV", nullptr, true)) {
				const auto before = object.meshRenderer;
				auto reset = *object.meshRenderer;
				reset.uvScale = {1.0f, 1.0f};
				reset.uvOffset = {0.0f, 0.0f};
				changed |=
				    history.SetMeshRenderer(id, reset) && object.meshRenderer != before;
			}

			ui.BeginDisabled(!object.meshRenderer->IsValid());

			if (ui.MenuItem("Copy UV", nullptr, true) && object.meshRenderer->IsValid()) {
				request.copyUv = true;
			}

			ui.EndDisabled();
			ui.BeginDisabled(!canPasteUv || !canApplyUvToSelection);

			if (ui.MenuItem("Paste UV to Selected Objects", nullptr, true) && canPasteUv &&
			    canApplyUvToSelection) {
				request.pasteUvToSelection = true;
			}

			ui.EndDisabled();
			ui.BeginDisabled(!canApplyUvToSelection);

			if (ui.MenuItem("Apply UV to Selected Objects", nullptr, true) && canApplyUvToSelection) {
				request.applyUvToSelection = true;
			}

			ui.EndDisabled();
			ImGui::EndPopup();
		}
		ImGui::TreePop();
	}

	{
		const auto model = !object.HasMesh()
		                       ? ModelHandle{}
		                       : models.FindLoaded(object.mesh->modelPath);
		const auto count = models.GetMaterialCount(model);
		const bool canClearUnused =
		    model && object.meshRenderer->IsValid() &&
		    std::any_of(object.meshRenderer->textures.slots.begin(),
		                object.meshRenderer->textures.slots.end(),
		                [&](const auto &binding) {
			                return models.GetMaterialMeshCount(
			                           model, binding.first) == 0;
		                });
		if (canClearUnused && ui.Button("Clear Unused Texture Assignments"))
			request.clearUnusedTextures = true;

		for (std::size_t slot = 0; slot < count; ++slot) {
			ImGui::PushID(static_cast<int>(slot));
			const auto materialName = models.GetMaterialName(model, slot);
			const auto meshes = models.GetMaterialMeshCount(model, slot);
			ui.Text(std::format("Material: {} | Meshes: {}{}",
			                    materialName.empty() ? "Unnamed" : materialName,
			                    meshes, meshes == 0 ? " [Unused]" : ""));
			changed |= DrawMaterialSlot(id, static_cast<std::uint32_t>(slot),
			                            object, history, assets);
			const auto found = std::find_if(
			    object.meshRenderer->textures.slots.begin(),
			    object.meshRenderer->textures.slots.end(),
			    [slot](const auto &binding) { return binding.first == slot; });
			const auto preview =
			    found == object.meshRenderer->textures.slots.end()
			        ? std::string("Inherit Texture")
			        : PathUtils::ToUtf8(found->second);
			const char *label = "Texture Override";

			if (ImGui::BeginCombo(label, preview.c_str())) {
				if (ui.Selectable(
				        "Inherit Texture",
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
		std::vector<std::uint32_t> unusedMaterials;
		for (const auto &[slot, path] : object.meshRenderer->materials.slots) {
			if (slot >= count)
				unusedMaterials.push_back(slot);
		}
		for (const auto slot : unusedMaterials) {
			ImGui::PushID(std::to_string(slot).c_str());
			ui.Text(std::format("Unused Material Slot {}", slot));
			changed |= DrawMaterialSlot(id, slot, object, history, assets);
			ImGui::PopID();
		}

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
