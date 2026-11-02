#include "appearance/MaterialSlotInspector.h"
#include "assets/AssetDatabase.h"
#include "graphics/model/ModelManager.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "platform/windows/PathUtils.h"
#include <algorithm>
#include <format>
#include <imgui.h>
#include <vector>

bool MaterialSlotInspector::Draw(const ImGuiSystem &ui, SceneObjectId id,
                                 SceneObject &object, EditorHistory &history,
                                 ModelManager &models,
                                 const AssetDatabase &assets) const {
	if (object.modelPath.empty() && object.materials.slots.empty()) {
		return false;
	}

	bool changed = false;
	ImGui::PushID(std::to_string(id).c_str());
	ImGui::PushID("MaterialSlots");

	if (ui.CollapsingHeader("Materials")) {
		const auto model = object.modelPath.empty()
		                       ? ModelHandle{}
		                       : models.Load(object.modelPath);
		const auto count = models.GetMaterialCount(model);

		for (std::size_t slot = 0; slot < count; ++slot) {
			ImGui::PushID(static_cast<int>(slot));
			const auto found = std::find_if(
			    object.materials.slots.begin(), object.materials.slots.end(),
			    [slot](const auto &binding) { return binding.first == slot; });
			const auto preview = found == object.materials.slots.end()
			                         ? std::string("Model Default")
			                         : PathUtils::ToUtf8(found->second);
			const auto label = std::format("Slot {}", slot);

			if (ImGui::BeginCombo(label.c_str(), preview.c_str())) {
				if (ui.Selectable("Model Default",
				                  found == object.materials.slots.end())) {
					changed |= history.SetMaterialPath(
					    id, static_cast<std::uint32_t>(slot), {});
				}

				for (const auto &path : assets.GetMaterials()) {
					const auto current =
					    std::find_if(object.materials.slots.begin(),
					                 object.materials.slots.end(),
					                 [slot](const auto &binding) {
						                 return binding.first == slot;
					                 });
					const auto name = PathUtils::ToUtf8(path);

					if (ui.Selectable(name.c_str(),
					                  current != object.materials.slots.end() &&
					                      current->second == path)) {
						changed |= history.SetMaterialPath(
						    id, static_cast<std::uint32_t>(slot), path);
					}
				}

				ImGui::EndCombo();
			}

			ImGui::PopID();
		}

		// モデルの差し替えで使用されなくなった割り当ても、勝手に破棄しない。
		std::vector<std::uint32_t> unused;

		for (const auto &[slot, path] : object.materials.slots) {
			if (slot >= count) {
				unused.push_back(slot);
			}
		}

		for (const auto slot : unused) {
			ImGui::PushID(std::format("unused{}", slot).c_str());
			ui.Text(std::format(
			    "Unused slot {}: {}", slot,
			    PathUtils::ToUtf8(std::find_if(object.materials.slots.begin(),
			                                   object.materials.slots.end(),
			                                   [slot](const auto &binding) {
				                                   return binding.first == slot;
			                                   })
			                          ->second)));

			if (ui.Button("Clear Assignment")) {
				changed |= history.SetMaterialPath(id, slot, {});
			}

			ImGui::PopID();
		}
	}

	ImGui::PopID();
	ImGui::PopID();
	return changed;
}
