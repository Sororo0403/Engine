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
	    {"Alt + left-drag", "Orbit around the center of selected object origins without snapping the starting view."},
	    {"Mouse wheel", "Move forward / backward. Click Scene first if it is not focused."},
	    {"F / Shift + F", "Frame selected models / all models."},
	    {"View > Performance", "Inspect recent frame intervals, FPS and live scene counts. Pause timing to freeze samples; Clear Timing starts a fresh sample window."},
	    {"View > Camera View", "Look from the front, back, left, right, top or bottom in perspective. Center on selected object origins, or the world origin when nothing is selected; keep the current distance."},
	    {"Hierarchy double-click", "Frame the clicked model."},
	});
	DrawSection("Selection and objects", {
	    {"Hierarchy > Sort", "Choose creation order or name A-Z / Z-A. Range selection, Select All and arrow keys follow the displayed order; changing sort keeps the selection."},
	    {"Inspector > Add Component > Box Collider", "Add a collider fitted to the model, edit its local center and half size, fit again or remove it. Green Scene bounds show the world-axis-aligned collision shape. Each drag is one Undo operation."},
	    {"Assets > Select Instances", "Select all scene objects using the chosen model, including hidden objects. Also available from a model row context menu; reveals the active result in Hierarchy."},
	    {"Assets folders / Up / Root", "Open a folder or return to its parent or the Assets root. Search finds model paths across all folders."},
	    {"Click / Ctrl + click", "Select one object / toggle an object in the selection."},
	    {"Drag in Scene", "Select models inside the rectangle."},
	    {"Ctrl + drag in Scene", "Add models inside the rectangle to the selection."},
	    {"Shift + click", "Select a range in Hierarchy. Search results limit the range."},
	    {"Up / Down / Home / End", "Select a Hierarchy item in the filtered display order. Home / End select the first / last item."},
	    {"Shift + navigation key", "Extend or shrink the Hierarchy range. Add Ctrl to keep the existing selection."},
	    {"Ctrl + Shift + click", "Add a Hierarchy range to the current selection."},
	    {"Ctrl + A", "Select visible Hierarchy items, or all models when Scene is focused."},
	    {"Esc", "Clear selection, or cancel a selection rectangle."},
	    {"Inspector > Lock Inspector", "Keep showing and editing one object while selection changes. F2 for one object unlocks Inspector; switching scenes clears the lock."},
	    {"F2", "Rename one object in Inspector, or preview numbered names for multiple selected objects. Batch rename follows Hierarchy order and supports Undo / Redo."},
	    {"Visible / Hierarchy menu", "Show or hide models without deleting objects. Visibility changes support Undo / Redo."},
	    {"Hierarchy > Hidden only", "List hidden models. Search and range selection use this filtered list."},
	    {"Edit > Show All Models", "Restore every hidden model in one undoable operation without changing selection."},
	    {"Edit > Reveal Active", "Clear Hierarchy filters and scroll to the active object without changing visibility or selection."},
	    {"Ctrl + D / Delete", "Duplicate / delete the selected objects."},
	    {"Shift + G", "Drop the selection to Y=0 using the lowest visible selected model bounds. Empty and hidden objects move with the group; relative positions are preserved."},
	    {"Align X / Y / Z to Active", "Use Edit or the Hierarchy menu to align selected origins to the active object's world coordinate on one axis. Other axes, rotation and scale stay unchanged."},
	    {"Distribute X / Y / Z", "For three or more selected objects, space origins evenly in world coordinate order with both endpoints fixed. Other axes, rotation and scale stay unchanged."},
	    {"Ctrl + C / X / V", "Copy / cut / paste scene objects."},
	});
	DrawSection("Transform gizmo", {
	    {"W / E / R", "Switch to Move / Rotate / Scale when not navigating the camera."},
	    {"X", "Toggle Local / World for single-object Move and Rotate."},
	    {"Scene toolbar", "Enable Snap and set movement, rotation and scale increments. Model drops also use the Move step."},
	    {"Scale center handle", "Scale multiple selected objects uniformly."},
	    {"Z / Center / Active", "For multiple selection, use the mean of object origins or the active object's origin as the gizmo pivot."},
	    {"Edit > Reset Transform", "Reset position, rotation and scale of selected objects."},
	    {"Right-click Transform", "Copy, paste or reset the inspected object's Transform. Transform copies keep the object clipboard intact."},
	});
	DrawSection("Model assets", {
	    {"Assets > Add to Scene", "Place the selected model at the world origin."},
	    {"Drag Assets into Scene", "Place a model at the drop position and select it."},
	    {"Drag into Inspector Model", "Replace the active object's model while keeping its transform."},
	    {"Click Inspector Model", "Find and select that model in Assets without changing the scene."},
	    {"Inspector > Remove Model", "Remove the model reference while keeping the object."},
	});
	DrawSection(
	    "Game authoring and playback",
	    {
	        {"Inspector > Add Component",
	         "Add Box Collider, Character Controller, Camera, Follow Camera or "
	         "Respawn. Changes support Undo and scene saving."},
	        {"File > New Sample Stage",
	         "Create an editable floor, walls, player and follow camera. "
	         "Unsaved work prompts before replacement."},
	        {"Game > Play / Stop",
	         "Run the current scene, including empty scenes. Stop returns to "
	         "editing and keeps your changes and Undo history."},
	        {"Game without Primary Camera",
	         "Use the Scene view camera position at Play start."},
	        {"Inspector > Camera",
	         "Use Transform for a fixed view. Set vertical FOV and clipping "
	         "distances; mark one Camera as Primary. Add Follow Camera to make "
	         "it track a target."},
	        {"Inspector > Follow Camera",
	         "Offsets use world coordinates. Follow Smoothing 0 follows "
	         "immediately. Choose the target object in the same scene."},
	        {"WASD / Space",
	         "Click the Game image to focus it. Move on world X/Z and jump "
	         "when the object has a Character Controller."},
	        {"Game > Pause / Resume",
	         "Freeze game time or continue from the paused state."},
	        {"Game > Step", "While paused, advance one game frame (1/60 "
	                        "second). Stay paused afterwards."},
	        {"Esc (Game focused)", "Stop playback and return to editing."},
	    });
	DrawSection("Files and history", {
	    {"Ctrl + N / Ctrl + O", "Create a scene / open a scene."},
	    {"Ctrl + S", "Save the scene."},
	    {"Ctrl + Shift + S", "Save the scene under another name."},
	    {"File > Reload Scene", "Confirm to reload the active file from disk. Success resets selection, camera and undo history; a failed load keeps the edited scene."},
	    {"File > Export Game", "Choose a new folder, game title and window size. Build Release first. Export keeps the active scene and saved state; run Game.exe from the exported folder."},
	    {"File > Save Copy", "Save a separate scene file and keep the active file and saved state."},
	    {"Ctrl + Z", "Undo the last edit."},
	    {"Ctrl + Y / Ctrl + Shift + Z", "Redo an edit."},
	    {"View > History", "Inspect operations, current and saved states. Click a state to undo or redo to that point."},
	    {"View > Console", "Filter recent logs by level or text, read details, copy a message or copy all matching logs with timestamps and levels. Clear Console resets recent messages while file output is kept."},
	    {"File > Open Recent", "Reopen a recently used scene, with a save prompt for unsaved changes."},
	});
	DrawSection("Editor settings", {
	    {"View > Camera Settings", "Adjust movement speed, acceleration and mouse sensitivity."},
	    {"View > Grid Settings", "Adjust grid spacing, extent and major-line interval."},
	    {"View menu", "Show or hide the grid and selection bounds, or reset the dock layout."},
	});
	ImGui::End();
}
