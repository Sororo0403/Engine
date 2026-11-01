#include "panels/ControlsPanel.h"

#include <imgui.h>

#include <initializer_list>

namespace {
/// <summary>
/// 操作入力と、その動作の説明
/// </summary>
struct Control {
	const char *input;
	const char *action;
};

/// <summary>
/// 折りたたみ可能な操作一覧を表示する
/// </summary>
/// <param name="title">項目の見出しとUIの識別名</param>
/// <param name="controls">入力と動作の一覧</param>
void DrawSection(const char *title, std::initializer_list<Control> controls) {
	if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}
	ImGui::PushID(title);
	if (ImGui::BeginTable("Controls", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
		ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthFixed, 190.0f);
		ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();
		for (const auto &control : controls) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(control.input);
			ImGui::TableNextColumn();
			ImGui::TextWrapped("%s", control.action);
		}
		ImGui::EndTable();
	}
	ImGui::PopID();
}
} // namespace

void ControlsPanel::Show() {
	open_ = true;
	focusRequested_ = true;
}

void ControlsPanel::Draw() {
	if (!open_) {
		return;
	}
	ImGui::SetNextWindowSize({640.0f, 580.0f}, ImGuiCond_FirstUseEver);
	if (focusRequested_) {
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
		focusRequested_ = false;
	}
	if (!ImGui::Begin("Editor Controls", &open_)) {
		ImGui::End();
		return;
	}
	ImGui::TextWrapped("Object shortcuts apply while Hierarchy or Scene is focused. Text fields keep their normal text-editing shortcuts.");
	DrawSection("Scene camera", {
	    {"Right-drag", "Look around. Start the drag over the Scene image."},
	    {"Right mouse + WASD", "Move the camera."},
	    {"Right mouse + Q / E", "Move down / up."},
	    {"Right mouse + Shift", "Move faster while navigating."},
	    {"Middle-drag", "Pan the camera."},
	    {"Mouse wheel", "Move forward / backward. Click Scene first if it is not focused."},
	    {"F / Shift + F", "Frame selected models / all models."},
	    {"Hierarchy double-click", "Frame the clicked model."},
	});
	DrawSection("Selection and objects", {
	    {"Click / Ctrl + click", "Select one object / toggle an object in the selection."},
	    {"Drag in Scene", "Select models inside the rectangle."},
	    {"Ctrl + drag in Scene", "Add models inside the rectangle to the selection."},
	    {"Shift + click", "Select a range in Hierarchy. Search results limit the range."},
	    {"Ctrl + Shift + click", "Add a Hierarchy range to the current selection."},
	    {"Ctrl + A", "Select visible Hierarchy items, or all models when Scene is focused."},
	    {"Esc", "Clear selection, or cancel a selection rectangle."},
	    {"F2", "Rename the single selected object in Inspector."},
	    {"Ctrl + D / Delete", "Duplicate / delete the selected objects."},
	    {"Ctrl + C / X / V", "Copy / cut / paste scene objects."},
	});
	DrawSection("Transform gizmo", {
	    {"W / E / R", "Switch to Move / Rotate / Scale when not navigating the camera."},
	    {"X", "Toggle Local / World for single-object Move and Rotate."},
	    {"Scene toolbar", "Enable Snap and set movement, rotation and scale increments."},
	    {"Scale center handle", "Scale multiple selected objects uniformly."},
	    {"Z / Center / Active", "For multiple selection, use the mean of object origins or the active object's origin as the gizmo pivot."},
	    {"Edit > Reset Transform", "Reset position, rotation and scale of selected objects."},
	});
	DrawSection("Model assets", {
	    {"Assets > Add to Scene", "Place the selected model at the world origin."},
	    {"Drag Assets into Scene", "Place a model at the drop position and select it."},
	    {"Drag into Inspector Model", "Replace the active object's model while keeping its transform."},
	    {"Inspector > Remove Model", "Remove the model reference while keeping the object."},
	});
	DrawSection("Files and history", {
	    {"Ctrl + N / Ctrl + O", "Create a scene / open a scene."},
	    {"Ctrl + S", "Save the scene."},
	    {"Ctrl + Shift + S", "Save the scene under another name."},
	    {"Ctrl + Z", "Undo the last edit."},
	    {"Ctrl + Y / Ctrl + Shift + Z", "Redo an edit."},
	    {"File > Open Recent", "Reopen a recently used scene, with a save prompt for unsaved changes."},
	});
	DrawSection("Editor settings", {
	    {"View > Camera Settings", "Adjust movement speed, acceleration and mouse sensitivity."},
	    {"View > Grid Settings", "Adjust grid spacing, extent and major-line interval."},
	    {"View menu", "Show or hide the grid and selection bounds, or reset the dock layout."},
	});
	ImGui::End();
}
