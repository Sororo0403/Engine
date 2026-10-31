#include "panels/HierarchyPanel.h"

#include "graphics/ui/ImGuiSystem.h"
#include "scene/Scene.h"

#include <format>

void HierarchyPanel::Draw(const ImGuiSystem &ui, Scene &scene,
                          SceneObjectId &selectedObjectId) const {
	// 外部の操作で削除された物体を選択したままにしない
	if (!scene.Contains(selectedObjectId)) {
		selectedObjectId = 0;
	}

	if (!ui.BeginWindow("Hierarchy")) {
		ui.EndWindow();
		return;
	}

	if (ui.Button("Create Empty")) {
		selectedObjectId = scene.CreateObject();
	}

	// 一覧の走査前に削除し、走査中のイテレーターを無効にしない
	if (selectedObjectId != 0 && ui.Button("Delete Selected")) {
		scene.DestroyObject(selectedObjectId);
		selectedObjectId = 0;
	}

	ui.Separator();

	const auto &objects = scene.GetObjects();

	if (objects.empty()) {
		ui.Text("No objects.");
	}

	for (const auto &[id, object] : objects) {
		// 表示名が同じでも、物体IDで項目を区別する。
		// ###以降は表示せず、UI内部の識別に使用する
		const auto label =
		    std::format("{}###SceneObject{}",
		                object.name.empty() ? "Object" : object.name, id);

		if (ui.Selectable(label.c_str(), selectedObjectId == id)) {
			selectedObjectId = id;
		}
	}

	ui.EndWindow();
}
