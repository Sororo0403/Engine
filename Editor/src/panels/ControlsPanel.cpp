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

	if (ImGui::BeginTable("Controls", 2,
	                      ImGuiTableFlags_RowBg |
	                          ImGuiTableFlags_BordersInnerV)) {
		ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthFixed,
		                        190.0f);
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

	ImGui::TextWrapped(
	    "Object shortcuts apply while Hierarchy or Scene is focused. Text "
	    "fields keep their normal text-editing shortcuts.");

	DrawSection(
	    "Scene camera",
	    {
	        {"Right-drag", "Look around. Start the drag over the Scene image."},
	        {"Right mouse + WASD", "Move the camera."},
	        {"Right mouse + Q / E", "Move down / up."},
	        {"Right mouse + Shift", "Move faster while navigating."},
	        {"Middle-drag", "Pan the camera."},
	        {"Alt + left-drag", "Orbit around the center of selected object "
	                            "origins without snapping the starting view."},
	        {"Mouse wheel", "Move forward / backward. Click Scene first if it "
	                        "is not focused."},
	        {"F / Shift + F", "Frame selected objects / all objects. Hidden "
	                          "models are excluded."},
	        {"View > Performance",
	         "Inspect recent frame intervals, FPS and live scene counts. Pause "
	         "timing to freeze samples; Clear Timing starts a fresh sample "
	         "window."},
	        {"View > Object Markers",
	         "Show small origin boxes for objects without models. Camera "
	         "markers are cyan, Collider markers green and Empty markers "
	         "gray; selected markers turn yellow. Click or box-select these "
	         "markers in Scene, using Ctrl to add or toggle selection. "
	         "The nearest model or marker wins a click. The marker size stays "
	         "fixed when the object is scaled; it does not represent collision "
	         "size. Turning markers off also disables their Scene selection. "
	         "Visibility applies to models; model-free markers remain "
	         "available. "
	         "Markers appear only in Scene and the display choice is saved."},
	        {"Model-free objects",
	         "Select Cameras, collider-only objects or Empty objects in "
	         "Hierarchy, then use the Scene gizmo to move, rotate or scale "
	         "them. F and Hierarchy double-click frame the Collider when "
	         "present, otherwise a unit box around the object origin. "
	         "Frame All includes model-free objects. These controls do not "
	         "add a model or collider; gizmo edits support Undo/Redo. "
	         "Scene clicking also selects model-free object markers."},
	        {"View > Camera View",
	         "Look from the front, back, left, right, top or bottom in "
	         "perspective. Center on selected object origins, or the world "
	         "origin when nothing is selected; keep the current distance."},
	        {"Hierarchy double-click", "Frame the clicked object. Model-free "
	                                   "objects use their Collider or origin."},
	    });

	DrawSection(
	    "Selection and objects",
	    {
	        {"Hierarchy > Sort",
	         "Choose creation order or name A-Z / Z-A. Range selection, Select "
	         "All and arrow keys follow the displayed order; changing sort "
	         "keeps the selection."},
	        {"Inspector > Add Component > Box Collider",
	         "Add a collider fitted to the model, edit its local center and "
	         "half size, fit again or remove it. Green Scene bounds show the "
	         "world-axis-aligned collision shape. Each drag is one Undo "
	         "operation."},
	        {"Assets > Select Instances",
	         "Select all scene objects using the chosen model, including "
	         "hidden objects. Also available from a model row context menu; "
	         "reveals the active result in Hierarchy."},
	        {"Assets folders / Up / Root",
	         "Open a folder or return to its parent or the Assets root. Search "
	         "finds model paths across all folders."},
	        {"Click / Ctrl + click",
	         "Select one object / toggle an object in the selection."},
	        {"Drag in Scene", "Select models inside the rectangle."},
	        {"Ctrl + drag in Scene",
	         "Add models inside the rectangle to the selection."},
	        {"Shift + click",
	         "Select a range in Hierarchy. Search results limit the range."},
	        {"Up / Down / Home / End",
	         "Select a Hierarchy item in the filtered display order. Home / "
	         "End select the first / last item."},
	        {"Shift + navigation key",
	         "Extend or shrink the Hierarchy range. Add Ctrl to keep the "
	         "existing selection."},
	        {"Ctrl + Shift + click",
	         "Add a Hierarchy range to the current selection."},
	        {"Ctrl + A", "Select visible Hierarchy items, or all models when "
	                     "Scene is focused."},
	        {"Esc", "Clear selection, or cancel a selection rectangle."},
	        {"Inspector > Lock Inspector",
	         "Keep showing and editing one object while selection changes. F2 "
	         "for one object unlocks Inspector; switching scenes clears the "
	         "lock."},
	        {"F2", "Rename one object in Inspector, or preview numbered names "
	               "for multiple selected objects. Batch rename follows "
	               "Hierarchy order and supports Undo / Redo."},
	        {"Visible / Hierarchy menu",
	         "Show or hide models without deleting objects. Visibility changes "
	         "support Undo / Redo."},
	        {"Play error > Show Object",
	         "Select the object reported by Play validation and reveal it in "
	         "Hierarchy. Inspector unlocks to show its settings. "
	         "Visibility is preserved and no Undo operation is added. "
	         "This action appears only for an existing object tied to the "
	         "error."},
	        {"Hierarchy > Search",
	         "Search names by case-sensitive substring. Enter id:42 to find "
	         "one exact object ID from a Play error or Inspector. "
	         "The ID prefix ignores letter case and surrounding whitespace. "
	         "Hidden only and component filters still apply. "
	         "Invalid or missing IDs show no matches. "
	         "Filtering never changes the scene or current selection."},
	        {"Hierarchy > Hidden only", "List hidden models. Search and range "
	                                    "selection use this filtered list."},
	        {"Edit > Show All Models",
	         "Restore every hidden model in one undoable operation without "
	         "changing selection."},
	        {"Edit > Reveal Active",
	         "Clear Hierarchy filters and scroll to the active object without "
	         "changing visibility or selection."},
	        {"Ctrl + D / Delete", "Duplicate / delete the selected objects."},
	        {"Deleting referenced objects",
	         "Remaining Follow Cameras lose targets that were deleted. Undo "
	         "restores the objects and their links together. Pasting a copy "
	         "keeps outside targets only if they still exist."},
	        {"Shift + G",
	         "Drop the selection to Y=0 using the lowest visible selected "
	         "model bounds. Empty and hidden objects move with the group; "
	         "relative positions are preserved."},
	        {"Align X / Y / Z to Active",
	         "Use Edit or the Hierarchy menu to align selected origins to the "
	         "active object's world coordinate on one axis. Other axes, "
	         "rotation and scale stay unchanged."},
	        {"Distribute X / Y / Z",
	         "For three or more selected objects, space origins evenly in "
	         "world coordinate order with both endpoints fixed. Other axes, "
	         "rotation and scale stay unchanged."},
	        {"Ctrl + C / X / V", "Copy / cut / paste scene objects."},
	    });

	DrawSection(
	    "Transform gizmo",
	    {
	        {"W / E / R",
	         "Switch to Move / Rotate / Scale when not navigating the camera."},
	        {"X", "Toggle Local / World for single-object Move and Rotate."},
	        {"Scene toolbar",
	         "Enable Snap and set movement, rotation and scale increments. "
	         "Model drops also use the Move step."},
	        {"Scale center handle",
	         "Scale multiple selected objects uniformly."},
	        {"Z / Center / Active",
	         "For multiple selection, use the mean of object origins or the "
	         "active object's origin as the gizmo pivot."},
	        {"Edit > Reset Transform",
	         "Reset position, rotation and scale of selected objects."},
	        {"Right-click Transform",
	         "Copy, paste or reset the inspected object's Transform. Transform "
	         "copies keep the object clipboard intact."},
	    });

	DrawSection(
	    "Model assets",
	    {
	        {"Assets > Add to Scene",
	         "Place the selected model at the world origin."},
	        {"Drag Assets into Scene",
	         "Place a model at the drop position and select it."},
	        {"Drag into Inspector Model",
	         "Replace the active object's model while keeping its transform."},
	        {"Click Inspector Model", "Find and select that model in Assets "
	                                  "without changing the scene."},
	        {"Inspector > Remove Model",
	         "Remove the model reference while keeping the object."},
	    });

	DrawSection(
	    "Game authoring and playback",
	    {
	        {"Inspector > Add Component",
	         "Add Box Collider, Character Controller, Camera, Follow Camera or "
	         "Respawn. Changes support Undo and scene saving."},
	        {"View > Colliders",
	         "Show collider outlines for None, Selected or All objects in "
	         "Scene. All includes hidden models and objects without models. "
	         "The outlines match world-axis collision boxes and never appear "
	         "in Game. This display setting is saved with Editor preferences."},
	        {"Create > Camera from Scene View",
	         "Create a fixed Camera at the current Scene view position and "
	         "orientation, using the standard Scene lens settings. It is "
	         "selected and revealed in Hierarchy in one Undo operation. If no "
	         "Primary exists, the new Camera becomes Primary; otherwise the "
	         "existing Primary is kept. Use "
	         "Inspector > Primary to switch. Hierarchy's background menu has "
	         "the same command."},
	        {"Create > Empty / Box",
	         "Add a normal object at the world origin and reveal it in "
	         "Hierarchy. Box uses the built-in two-unit model with a matching "
	         "Box Collider. Scale it into floors, walls or platforms. Creation "
	         "supports Undo/Redo and scene saving. Hierarchy has the same "
	         "creation controls."},
	        {"Hierarchy > Component",
	         "Combine component filters with Search and Hidden only. Models "
	         "without Collider finds model objects missing collision shapes. "
	         "Ctrl+A and range selection use the filtered list. Existing "
	         "selections remain selected when filtered out; Reveal Active "
	         "clears filters to show the active object."},
	        {"Edit > Drop Colliders to Surface",
	         "Move selected Box Colliders straight down until the first one "
	         "reaches another collider's top surface. Selection spacing is "
	         "preserved; hidden colliders also count. Collision uses the same "
	         "world-axis boxes as Play, with a tiny clearance. All selected "
	         "objects need colliders. No surface or an existing overlap leaves "
	         "them unchanged. The action supports Undo/Redo; Hierarchy's "
	         "context menu has the same command. Drop to Ground still aligns "
	         "visible model bounds to Y=0."},
	        {"Edit > Box Colliders",
	         "Apply to all selected objects, including hidden ones. Add "
	         "Missing keeps existing collider shapes. Fit All to Models "
	         "replaces or adds shapes; objects without models use a unit box. "
	         "Remove All keeps other components. Each operation supports one "
	         "Undo/Redo; a failed operation changes no colliders."},
	        {"File > New Sample Stage",
	         "Create an editable floor, walls, player and follow camera. "
	         "Unsaved work prompts before replacement."},
	        {"Game while editing",
	         "Preview the authored scene from the Primary Camera without "
	         "running physics. Follow Cameras show their starting poses. "
	         "Without a Primary, use the Scene view position. The image uses "
	         "Game Settings resolution and aspect. Invalid camera chains clear "
	         "the preview; incomplete movement/collider settings do not "
	         "require Play."},
	        {"Game > Play / Stop",
	         "Run the current scene, including empty scenes. Stop returns to "
	         "editing and keeps your changes and Undo history."},
	        {"F5 / F6 / F7 / F8",
	         "Play or Stop / Pause or Resume / Step while paused / Restart. "
	         "Use without Ctrl, Shift or Alt. Shortcuts are ignored while "
	         "editing text or fields, dragging, showing a popup, or when "
	         "Editor is not active. Holding a key does not repeat the action."},
	        {"Scene during Play",
	         "Observe the live scene from an independent camera. Right-drag "
	         "and WASD, middle-drag, wheel and Alt-orbit still work, including "
	         "while paused. F / Shift+F frame current runtime positions. "
	         "Grid, Collider display, object markers and selection bounds "
	         "apply only to Scene; Game keeps its own camera and clean image. "
	         "Editing, selection changes and model drops are disabled. Scene "
	         "camera controls do not move the player. Play continues when "
	         "Game is hidden but Scene is visible; hiding both or switching "
	         "away from the application suspends simulation. Stop restores "
	         "the unchanged editing scene; the Scene camera view is kept."},
	        {"View > Play State",
	         "Inspect the live character position, vertical velocity and "
	         "grounded state, plus the rendered camera pose and lens. The "
	         "panel is read-only and can be opened during Play. Pause freezes "
	         "the values; Step shows the next simulated frame. Stop clears the "
	         "live display."},
	        {"Game > Restart",
	         "Reset the player, physics and cameras to the state captured when "
	         "Play started. The first frame shows that initial state. Restart "
	         "keeps Pause, discards a queued Step and returns focus to Game. "
	         "The editing scene and Undo history stay unchanged."},
	        {"Game without Primary Camera",
	         "Use the Scene view camera position at Play start."},
	        {"Inspector > Camera",
	         "Use Transform for a fixed view. Set vertical FOV and clipping "
	         "distances; mark one Camera as Primary. Add Follow Camera to make "
	         "it track a target."},
	        {"Inspector > Camera > Primary",
	         "Turning Primary on clears it on other Cameras in one Undo "
	         "operation. Leave all Cameras non-Primary to use the Scene view "
	         "camera at Play start. Adding a Camera makes it Primary; "
	         "duplicate and pasted Cameras keep the existing Primary."},
	        {"Inspector > Camera > Align to Scene View",
	         "Copy the Scene view position and direction to the inspected "
	         "Camera. Follow Camera uses world Y as up and updates its offsets "
	         "in one Undo operation. Lens, scale and follow smoothing are "
	         "preserved. Follow Camera needs a target, a view not straight up "
	         "or down, and offsets within its allowed range."},
	        {"View > Camera Guides / Respawn Guides",
	         "Toggle selected Camera frustums and follow links, or Respawn "
	         "position and fall-height markers. Each setting is saved for "
	         "the next Editor session. These settings affect only Scene "
	         "guides; they do not change Game rendering or gameplay."},
	        {"Selected Camera in Scene",
	         "The gold wireframe shows Near/Far Clip and the field of view "
	         "at the aspect ratio in Game Settings. Follow Camera previews "
	         "its starting view using the current target position."},
	        {"Selected Follow Camera in Scene",
	         "The cyan cross marks the target's starting position. "
	         "The cyan line connects it to the Camera. "
	         "The gold cross marks Look Offset; its line shows the view "
	         "direction. "
	         "Follow chains use their resolved starting positions. "
	         "These guides appear only in Scene and do not change the scene."},
	        {"Inspector > Camera > Reset Lens Settings",
	         "Restore the default FOV and Near/Far Clip in one Undo operation. "
	         "Primary, Transform and Follow Camera settings stay unchanged. "
	         "Pending edits finish first. Default values add no history."},
	        {"Inspector > Reset Character / Follow / Respawn Settings",
	         "Reset this component to defaults in one Undo operation. "
	         "Follow Camera keeps its target; offsets and smoothing reset. "
	         "Respawn uses the start position and default fall height. "
	         "Transform and other components stay unchanged. "
	         "Pending edits finish first. Default values add no history."},
	        {"Inspector > Follow Camera",
	         "Offsets use world coordinates. Follow Smoothing 0 follows "
	         "immediately. Choose the target object in the same scene; IDs "
	         "distinguish matching names. None clears the target. "
	         "Self-reference, "
	         "circular chains and broken target chains cannot be selected. "
	         "Target changes support Undo/Redo."},
	        {"Hierarchy menu > Select Follow Cameras",
	         "Select objects that directly follow any selected target. "
	         "Hidden objects and incomplete Follow Camera components are "
	         "included. "
	         "Indirect followers are not included. Hierarchy filters clear "
	         "to reveal the result; visibility and Inspector locks stay "
	         "unchanged. "
	         "Selection adds no Undo operation and pending edits finish "
	         "first."},
	        {"Inspector > Follow Camera > Select Target",
	         "Select only the referenced object and reveal it in Hierarchy. "
	         "This clears Hierarchy filters but keeps hidden models hidden. "
	         "An Inspector lock stays on its original object. "
	         "Selection adds no Undo operation; pending edits finish first. "
	         "Unavailable when the target is missing or refers to itself."},
	        {"Inspector > Follow Camera > Target > Search",
	         "Filter targets by name or ID. ASCII letter case is ignored. "
	         "Hidden objects remain valid candidates. None always clears the "
	         "target. Search and cancellation do not change the scene; "
	         "choosing a different target creates one Undo operation. "
	         "Reopening starts a fresh search."},
	        {"Selected Respawn in Scene",
	         "The cyan cross marks the respawn position. The red square marks "
	         "Fall Below Y near that point; the height applies everywhere, "
	         "not just inside the square. This preview is not a collider and "
	         "does not appear in Game."},
	        {"Multiple Follow Cameras",
	         "Each Camera keeps its own smoothing state. Targets that are also "
	         "Follow Cameras update first. Circular target chains must be "
	         "corrected before Play. Choose the rendered Camera using "
	         "Primary."},
	        {"WASD / Space",
	         "Click the Game image to focus it. Move on world X/Z and jump "
	         "when the object has a Character Controller."},
	        {"Game > Pause / Resume",
	         "Freeze game time or continue from the paused state."},
	        {"Game > Step", "While paused, advance one game frame (1/60 "
	                        "second). Stay paused afterwards."},
	        {"Esc (Game focused)", "Stop playback and return to editing."},
	    });

	DrawSection(
	    "Files and history",
	    {
	        {"References when duplicating / pasting",
	         "References between copied objects point to the new copies. Other "
	         "targets stay unchanged in the same scene. New/Open/Reload clears "
	         "clipboard references to objects outside the copied group."},
	        {"Ctrl + N / Ctrl + O", "Create a scene / open a scene."},
	        {"Ctrl + S", "Save the scene."},
	        {"Ctrl + Shift + S", "Save the scene under another name."},
	        {"File > Reload Scene",
	         "Confirm to reload the active file from disk. Success resets "
	         "selection, camera and undo history; a failed load keeps the "
	         "edited scene."},
	        {"File > Game Settings",
	         "Set the game title and render size before Play or export. Apply "
	         "keeps the scene and its undo history unchanged. Settings are "
	         "saved when Editor closes."},
	        {"File > Export Game",
	         "Choose a new folder, game title and window size. Build Release "
	         "first. Export keeps the active scene and saved state; run "
	         "Game.exe from the exported folder."},
	        {"File > Save Copy", "Save a separate scene file and keep the "
	                             "active file and saved state."},
	        {"Ctrl + Z", "Undo the last edit."},
	        {"Ctrl + Y / Ctrl + Shift + Z", "Redo an edit."},
	        {"View > History", "Inspect operations, current and saved states. "
	                           "Click a state to undo or redo to that point."},
	        {"View > Console",
	         "Filter recent logs by level or text, read details, copy a "
	         "message or copy all matching logs with timestamps and levels. "
	         "Clear Console resets recent messages while file output is kept."},
	        {"File > Open Recent", "Reopen a recently used scene, with a save "
	                               "prompt for unsaved changes."},
	    });

	DrawSection(
	    "Editor settings",
	    {
	        {"View > Camera Settings",
	         "Adjust movement speed, acceleration and mouse sensitivity."},
	        {"View > Grid Settings",
	         "Adjust grid spacing, extent and major-line interval."},
	        {"View menu", "Show or hide the grid and selection bounds, or "
	                      "reset the dock layout."},
	    });

	ImGui::End();
}
