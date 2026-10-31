#include "panels/HierarchyPanel.h"

#include "graphics/ui/ImGuiSystem.h"
#include "scene/Scene.h"

#include <format>

bool HierarchyPanel::Draw(const ImGuiSystem &ui, Scene &scene,
                          SceneObjectId &selectedObjectId) const {
	if (!scene.Contains(selectedObjectId)) {
		selectedObjectId = 0;
	}

	if (!ui.BeginWindow("Hierarchy")) {
		ui.EndWindow();
		return false;
	}

	bool changed = false;

	if (ui.Button("Create Empty")) {
		selectedObjectId = scene.CreateObject();
		changed = true;
	}

	// 一覧の走査前に削除する
	if (selectedObjectId != 0 && ui.Button("Delete Selected")) {
		if (scene.DestroyObject(selectedObjectId)) {
			changed = true;
		}

		selectedObjectId = 0;
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
	return changed;
}
