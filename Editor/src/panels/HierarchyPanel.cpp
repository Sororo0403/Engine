#include "panels/HierarchyPanel.h"

#include "graphics/ui/ImGuiSystem.h"
#include "scene/Scene.h"

#include <imgui.h>

#include <format>

HierarchyPanel::Result HierarchyPanel::Draw(const ImGuiSystem &ui,
                                            const Scene &scene,
                                            SceneObjectId &selectedObjectId) {
	Result result;

	// 検索で隠れただけの場合は、選択を解除しない
	if (!scene.Contains(selectedObjectId)) {
		selectedObjectId = 0;
	}

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

	// 非表示の物体が選択されている場合も、削除対象を確認できるようにする
	const SceneObject *selected = scene.FindObject(selectedObjectId);

	if (selected) {
		const std::string_view name = selected->name.empty()
		                                  ? std::string_view{"Object"}
		                                  : std::string_view{selected->name};

		if (!MatchesFilter(name)) {
			ui.Text(std::format("Selected (hidden): {}", name));
		}
	}

	if (ui.Button("Create Empty")) {
		result.action = Action::CREATE_EMPTY;
	}

	if (selectedObjectId != 0 && ui.Button("Delete Selected")) {
		result.action = Action::DELETE_OBJECT;
		result.objectId = selectedObjectId;
	}

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

			// 名前や検索条件が変わっても、物体IDで項目を識別する
			const auto label = std::format("{}###SceneObject{}", name, id);

			if (ui.Selectable(label.c_str(), selectedObjectId == id)) {
				selectedObjectId = id;
			}
		}

		if (!anyVisible) {
			ui.Text(objects.empty() ? "No objects." : "No matching objects.");
		}
	}

	// BeginChildがfalseを返した場合も終了処理を呼び出す
	ui.EndChild();

	// 一覧の子領域にフォーカスがある場合もHierarchyの操作として扱う
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
