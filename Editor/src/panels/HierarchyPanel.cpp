#include "panels/HierarchyPanel.h"

#include "graphics/ui/ImGuiSystem.h"
#include "scene/Scene.h"

#include <format>

HierarchyPanel::Result
HierarchyPanel::Draw(const ImGuiSystem &ui, const Scene &scene,
                     SceneObjectId &selectedObjectId) const {
	Result result;

	if (!scene.Contains(selectedObjectId)) {
		selectedObjectId = 0;
	}

	if (!ui.BeginWindow("Hierarchy")) {
		ui.EndWindow();
		return result;
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

	if (objects.empty()) {
		ui.Text("No objects.");
	}

	for (const auto &[id, object] : objects) {
		// 表示名が変わっても物体IDで項目を識別する
		const auto label =
		    std::format("{}###SceneObject{}",
		                object.name.empty() ? "Object" : object.name, id);

		if (ui.Selectable(label.c_str(), selectedObjectId == id)) {
			selectedObjectId = id;
		}
	}

	ui.EndWindow();
	return result;
}
