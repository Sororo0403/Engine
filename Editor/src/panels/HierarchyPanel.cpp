#include "panels/HierarchyPanel.h"

#include "graphics/ui/ImGuiSystem.h"
#include "scene/Scene.h"
#include "selection/EditorSelection.h"

#include <imgui.h>

#include <format>

HierarchyPanel::Result HierarchyPanel::Draw(const ImGuiSystem &ui,
                                            const Scene &scene,
                                            EditorSelection &selection) {
	Result result;

	// 検索で隠れた物体は残し、削除済みの物体だけ取り除く
	result.selectionChanged = selection.RemoveMissing(scene);

	if (!ui.BeginWindow("Hierarchy")) {
		ui.EndWindow();
		return result;
	}

	ui.InputText("Search", filter_);

	ui.SameLine();
	ui.BeginDisabled(filter_.empty());

	if (ui.Button("Clear")) {
		Clear();
	}

	ui.EndDisabled();

	ui.Text(std::format("Selected: {}", selection.GetCount()));

	const SceneObject *active = scene.FindObject(selection.GetActiveId());

	if (active) {
		const std::string_view name = active->name.empty()
		                                  ? std::string_view{"Object"}
		                                  : std::string_view{active->name};

		ui.Text(std::format("Active: {}", name));
	}

	std::size_t hiddenCount = 0;

	for (const SceneObjectId id : selection.GetIds()) {
		const SceneObject *object = scene.FindObject(id);

		if (!object) {
			continue;
		}

		const std::string_view name = object->name.empty()
		                                  ? std::string_view{"Object"}
		                                  : std::string_view{object->name};

		if (!MatchesFilter(name)) {
			++hiddenCount;
		}
	}

	if (hiddenCount != 0) {
		ui.Text(std::format("Hidden selections: {}", hiddenCount));
	}

	if (selection.GetCount() > 1) {
		ui.Text("Inspector: active object only.");
	}

	if (ui.Button("Create Empty")) {
		result.action = Action::CREATE_EMPTY;
	}

	ui.BeginDisabled(selection.GetCount() == 0);

	if (ui.Button("Delete Selected")) {
		result.action = Action::DELETE_OBJECTS;
	}

	ui.EndDisabled();

	ui.Separator();

	const auto &objects = scene.GetObjects();

	const bool drawObjects = ui.BeginChild("HierarchyObjects", 0.0f, 0.0f);

	if (drawObjects) {
		bool anyVisible = false;

		for (const auto &[id, object] : objects) {
			const std::string_view name = object.name.empty()
			                                  ? std::string_view{"Object"}
			                                  : std::string_view{object.name};

			if (!MatchesFilter(name)) {
				continue;
			}

			anyVisible = true;

			// アクティブ物体に印を付けても、項目のIDは変えない
			const auto label = std::format(
			    "{}{}###SceneObject{}",
			    selection.GetActiveId() == id ? "> " : "  ", name, id);

			if (ui.Selectable(label.c_str(), selection.Contains(id))) {
				const bool changed = ImGui::GetIO().KeyCtrl
				                         ? selection.Toggle(id)
				                         : selection.SelectOnly(id);

				if (changed) {
					result.selectionChanged = true;
				}
			}
		}

		if (!anyVisible) {
			ui.Text(objects.empty() ? "No objects." : "No matching objects.");
		}
	}

	ui.EndChild();

	result.windowFocused =
	    ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows |
	                           ImGuiFocusedFlags_NoPopupHierarchy);

	ui.EndWindow();
	return result;
}

void HierarchyPanel::Clear() { filter_.clear(); }

bool HierarchyPanel::MatchesFilter(std::string_view name) const {
	return filter_.empty() || name.find(filter_) != std::string_view::npos;
}
