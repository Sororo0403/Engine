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
	        {"Edit > Duplicate with Offset",
	         "Repeat selected objects as a group in world coordinates. Copies "
	         "counts additional groups; group 1 uses World Offset, group 2 "
	         "uses twice that offset, and so on. Originals stay unchanged. "
	         "Hidden objects and all components are copied; follow targets "
	         "inside each group point to that group's copies. External targets "
	         "and explicit Respawn positions stay unchanged. Existing Primary "
	         "Cameras are kept. Up to 100 groups and 10000 new objects can be "
	         "created in one Undo/Redo. New objects are selected and revealed "
	         "in Hierarchy. Cancel changes nothing. The Hierarchy context menu "
	         "has the same action."},
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
	         "Model drops use the Move step along the placement surface. "
	         "The contact axis keeps its surface position."},
	        {"Scale center handle",
	         "Scale multiple selected objects uniformly."},
	        {"Z / Center / Active",
	         "For multiple selection, use the mean of object origins or the "
	         "active object's origin as the gizmo pivot."},
	        {"Mirrored objects",
	         "Objects with negative Scale can use Move, Rotate and Scale "
	         "gizmos. Local handles follow the object's rotation; scaling "
	         "changes "
	         "the size while keeping each axis's reflection sign. Use the "
	         "Inspector Scale fields to change reflection signs. Zero scale "
	         "is not allowed. Gizmo edits support Undo/Redo."},
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
	         "Place a model against the nearest Box Collider surface and "
	         "select it. "
	         "Collider placement uses the same world-axis box as physics, "
	         "including hidden objects. Without a Collider hit, use Y=0 or "
	         "a point in front of the camera. The model bounds stay outside "
	         "the surface; its rotation stays unchanged. Undo removes the "
	         "new object and Redo restores its placement."},
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
	        {"Box Colliders > Place Next to Active",
	         "Select at least two Box Colliders. The active object stays "
	         "fixed; the other selected objects move as one group to its "
	         "Right, Left, Above, Below, Front or Behind face in world axes. "
	         "This uses the same axis-aligned collider bounds as Play, "
	         "including rotation, scale and hidden objects, with a tiny "
	         "clearance for rounding. Group spacing and other axes are kept. "
	         "Already aligned faces add no history. The move is one Undo/Redo "
	         "operation."},
	        {"Edit > Box Colliders",
	         "Apply to all selected objects, including hidden ones. Add "
	         "Missing keeps existing collider shapes. Fit All to Models "
	         "replaces or adds shapes; objects without models use a unit box. "
	         "Remove All keeps other components. Each operation supports one "
	         "Undo/Redo; a failed operation changes no colliders."},
	        {"Mesh and Mesh Renderer components",
	         "Mesh stores the model's geometry reference. Mesh Renderer stores "
	         "visibility and material slot assignments. Add or remove them "
	         "independently from Inspector. Removing Mesh Renderer keeps the "
	         "Mesh reference; removing Mesh keeps the renderer settings. Both "
	         "components and an assigned Mesh are needed to draw. Clear Mesh "
	         "Reference keeps the Mesh component. Asset placement creates "
	         "both. "
	         "Component changes, visibility and material assignments support "
	         "Undo/Redo. Older scenes are converted on load. Objects with Mesh "
	         "alone can still be selected and framed using their editor "
	         "marker. "
	         "Hierarchy component filters find Mesh and Mesh Renderer "
	         "separately."},
	        {"Materials and material slots",
	         "Create a .material.json asset in Materials, edit Base Color, UV "
	         "and Texture, then Save Material. Duplicate Material uses New "
	         "Material "
	         "Name to create and open a separate copy of the saved material. "
	         "Save or revert pending edits first; reload external changes "
	         "before duplicating. Existing files are never overwritten. "
	         "Texture references are adjusted for the new file location; "
	         "the image is shared. Scene slot assignments and the source "
	         "material are unchanged. Texture paths are relative to "
	         "the material file; empty uses white. Texture Asset selects PNG, "
	         "JPEG, BMP, TIFF or GIF from the resource tree; Refresh Textures "
	         "updates that list without discarding drafts. None clears the "
	         "texture. Picking changes the draft only; Save Material applies "
	         "it and Undo Material / Redo Material restore saved choices. "
	         "The existing Texture field also accepts relative paths. "
	         "Inspector > Mesh Renderer "
	         "assigns "
	         "an asset to each model slot; Model Default restores its imported "
	         "material. One asset can be shared by different objects and "
	         "models. "
	         "Saving updates every user of that asset without changing meshes. "
	         "Assignment supports scene Undo/Redo. Undo Material and Redo "
	         "Material "
	         "restore saves in the Materials panel; scene history does not "
	         "edit "
	         "asset files. Revert Draft discards unsaved edits. Reload "
	         "Material "
	         "accepts external changes. The title marks unsaved material "
	         "drafts. "
	         "Closing asks to save material and scene changes separately; "
	         "Cancel keeps pending drafts and a failed save keeps Editor open. "
	         "Play displays assignments read-only. "
	         "Edit Material on a slot opens its assigned asset in Materials, "
	         "including unused assignments. The fixed Inspector uses its own "
	         "object. Opening another asset asks to save pending material "
	         "edits; "
	         "Cancel or a failed load keeps them. Opening the same material "
	         "focuses its existing draft. Model Default has no editable asset. "
	         "Select Objects Using Material selects all explicit assignments, "
	         "including hidden objects, objects without Mesh and unused slots. "
	         "The current active object is kept when it is a user. Hierarchy "
	         "reveals the result; no users clears selection. Draft material "
	         "edits, scene data and Undo history are kept. Model Default "
	         "references are not explicit assignments. "
	         "Export asks to save a dirty material only when the scene uses "
	         "it, "
	         "including hidden objects and unused slots. Save writes the draft "
	         "before opening export; a failed save keeps the draft and blocks "
	         "export. Don't Save uses the saved file without discarding the "
	         "draft. Cancel stops before the export dialog. Scene edits are "
	         "exported without requiring a scene save. "
	         "Game export includes assigned material files and their "
	         "textures."},
	        {"Scene / Material / Settings save failures",
	         "A save uses a newly created file and replaces the destination "
	         "after writing finishes. An existing staging file is never "
	         "overwritten or removed. A failed replacement keeps the old "
	         "destination and removes only the file created for this attempt. "
	         "Close applications that lock the destination, then retry or "
	         "choose another destination. New material creation also refuses "
	         "to overwrite an existing asset."},
	        {"Scene save conflicts",
	         "Save compares the active file with its last opened or saved "
	         "contents. External edits, including formatting changes, block "
	         "overwriting it. Missing or unreadable active files also block "
	         "Save. Your scene, destination and saved history point are kept. "
	         "Use Save As or Save Copy to a different file to preserve your "
	         "editing work, or Reload to read the external version after "
	         "confirming unsaved changes. Save Copy keeps the active "
	         "destination and its comparison baseline."},
	        {"Game > Playback Speed",
	         "Slow Play to 0.1x, 0.25x or 0.5x, or return to 1x. Movement, "
	         "gravity and camera following use the same scaled game time. "
	         "Pause freezes the simulation; Step always advances 1/60 second "
	         "at any speed. Restart preserves speed and pause but clears a "
	         "pending Step. Stop and a new Play reset speed to 1x. Long real "
	         "frames are capped at 0.1 second before scaling. This is an "
	         "Editor test control: scene data, movement settings, history, "
	         "preferences and exported games are unchanged. Speed changes "
	         "are unavailable while another dialog is open."},
	        {"Game > Stop and Keep Selected Transforms",
	         "Select objects in the playing Hierarchy or Scene, then stop "
	         "with this button to keep their current position, rotation and "
	         "scale in the editing scene. All selected objects must exist "
	         "in both scenes and have valid Transforms. Failure keeps Play "
	         "and all editing Transforms unchanged. One Undo/Redo restores "
	         "the batch; identical values stop without adding history. "
	         "Other objects, components, editing selection, Inspector locks "
	         "and clipboard data are preserved. Follow Camera offsets and "
	         "Respawn settings are not copied. Normal Stop discards runtime "
	         "changes. No automatic save occurs. Pause first to inspect a "
	         "specific frame. Unavailable without runtime selection or while "
	         "another dialog is open."},
	        {"Materials > Shading",
	         "Lit applies directional and ambient lighting. Unlit shows the "
	         "texture, Base Color and per-object Color Tint without lighting; "
	         "it does not cast light onto other objects. Choose a mode, then "
	         "Save Material to update all shared users. Revert Draft and the "
	         "material Undo/Redo include this setting. Transparency, depth, "
	         "UV and texture behavior stay the same. Old version 1 materials "
	         "load as Lit; saved assets use version 2. Game export keeps the "
	         "saved shading mode and the matching shader."},
	        {"Materials > Assign Slot to Selection",
	         "Save the material, choose its zero-based Material Slot, then "
	         "assign it to all selected objects. Every selected object needs "
	         "Mesh Renderer; Mesh is optional. Hidden objects and unused "
	         "slots are included. Reset Slot to Model Default removes only "
	         "that slot override, even while a material draft is pending. "
	         "Other slots, visibility, meshes, transforms, selection and "
	         "Inspector locks are kept. Each batch is one Undo/Redo; identical "
	         "values add no history. External material changes require reload "
	         "before assigning. Material files and drafts are not edited by "
	         "scene Undo/Redo."},
	        {"Inspector > Color Tint",
	         "Add Color Tint from Add Component to multiply the model's "
	         "original material and texture by an RGB color. White restores "
	         "the original appearance. Alpha and UV settings are preserved. "
	         "Color edits, Reset Color and removal support Undo/Redo, scene "
	         "saving, duplication and Play. Objects sharing one model keep "
	         "independent colors. Hierarchy > Component > Color Tint finds "
	         "objects with the component. During Play it is read-only."},
	        {"Edit > Box Colliders > Paste Box Colliders",
	         "Apply the copied local collider shape to every selected object, "
	         "including hidden objects and objects without models. Missing "
	         "colliders are added; existing shapes are replaced. All targets "
	         "must accept the shape or none change. Each target keeps its "
	         "Transform and other components. Selection, Inspector locks and "
	         "all clipboards are kept. One Undo/Redo restores the whole edit; "
	         "identical values add no history. Hierarchy has the same action. "
	         "Copy a Box Collider in Inspector first."},
	        {"Inspector > Box Collider > Copy / Paste Box Collider",
	         "Copy the local Center and Half Size, then paste them onto the "
	         "inspected object. Add Component > Paste Box Collider adds a "
	         "missing component. The target's Transform, other components, "
	         "selection and Inspector lock stay unchanged; target rotation and "
	         "scale still determine the world collision box. Paste supports "
	         "one Undo/Redo; identical values add no history. Invalid target "
	         "bounds reject the paste. Object and Transform clipboards are "
	         "independent and are kept. The collider copy also survives scene "
	         "changes. These editing actions are unavailable during Play."},
	        {"Inspector > Box Collider > Frame Collider",
	         "Bring the inspected collider's actual collision box into Scene "
	         "view, including its center offset, rotation and scale. Models "
	         "may be hidden or absent; a locked Inspector uses its fixed "
	         "object. The Scene tab opens if necessary. Selection, collider "
	         "display, object data and Undo history stay unchanged. View > "
	         "Colliders > All shows hidden or unselected shapes. Invalid "
	         "colliders disable the action. Bounds outside the camera's "
	         "clipping range leave the camera unchanged."},
	        {"Inspector > Box Collider > Select Overlapping Colliders",
	         "Select the other colliders sharing volume with the inspected "
	         "object at its current position. Use this to find solids around "
	         "a character's start position. Hidden objects count; self and "
	         "face-only contact do not. No overlaps keeps the current "
	         "selection. Hierarchy reveals the active result and Inspector "
	         "locks are preserved. View > Colliders > Selected shows selected "
	         "shapes even when their models are hidden. Selecting adds no "
	         "history; pending edits finish first. Static obstacles may "
	         "intentionally overlap each other."},
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
	        {"View > Scene Check",
	         "Check the current editing scene without starting Play. Results "
	         "list component, follow-camera, start-position and recovery-point "
	         "issues; Select Object reveals a result in Hierarchy and keeps "
	         "Inspector locks. Results describe the last check: use Check "
	         "Scene "
	         "after edits. Fix invalid prerequisites before rechecking "
	         "dependent "
	         "collision rules. Empty scenes are valid. This checks playback "
	         "settings plus model/material loading and dependency-file "
	         "existence. "
	         "Hidden meshes, renderer-only material assignments and unused "
	         "slots are included. Model dependencies include OBJ material "
	         "files and glTF buffers and images. Shared file results are "
	         "reused during each check. Image decoding, GPU rendering and "
	         "export-folder restrictions are not checked. Scene data and Undo "
	         "history are preserved."},
	        {"Game > Play / Stop",
	         "Run the current scene, including empty scenes. Stop returns to "
	         "editing and keeps your changes and Undo history."},
	        {"F5 / F6 / F7 / F8",
	         "Play or Stop / Pause or Resume / Step while paused / Restart. "
	         "Use without Ctrl, Shift or Alt. Shortcuts are ignored while "
	         "editing text or fields, dragging, showing a popup, or when "
	         "Editor is not active. Holding a key does not repeat the action."},
	        {"Play > Inspector > Copy Transform",
	         "Copy the active runtime object's current position, rotation and "
	         "scale while running or paused. Stop, inspect the desired editing "
	         "object, then right-click Transform > Paste Transform. Paste is "
	         "one Undo operation. The copy is a snapshot: later motion, "
	         "Restart and Stop do not change it. Object clipboard contents "
	         "are kept; copying alone changes neither scene nor Undo history. "
	         "Component settings such as Follow Camera offsets are not "
	         "copied."},
	        {"Runtime selection",
	         "Select running objects in Scene or Hierarchy to inspect their "
	         "current Transform and component values. Click, Ctrl-click and "
	         "box/range selection work; F frames the runtime selection. "
	         "Inspector is read-only during Play. Runtime selection and "
	         "Hierarchy filters are separate from editing; Stop returns to "
	         "the original editing selection and Inspector lock. Selecting "
	         "objects adds no Undo history and cannot change the game."},
	        {"Scene during Play",
	         "Observe the live scene from an independent camera. Right-drag "
	         "and WASD, middle-drag, wheel and Alt-orbit still work, including "
	         "while paused. F / Shift+F frame current runtime positions. "
	         "Grid, Collider display, object markers and selection bounds "
	         "apply only to Scene; Game keeps its own camera and clean image. "
	         "Editing and model drops are disabled. Scene "
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
	        {"Inspector > Respawn > Paste Respawn Position",
	         "Use the position from the copied Transform as an explicit "
	         "recovery "
	         "point. You can copy a live position during Play, Stop, then "
	         "paste "
	         "it here. The inspected object is used even when Inspector is "
	         "locked. This turns off Use Start Position and supports one "
	         "Undo/Redo. Object Transform, fall height, selection and "
	         "clipboard "
	         "contents stay unchanged. Collision checks still apply when Play "
	         "starts. Copy a Transform first to enable the action."},
	        {"Inspector > Respawn > Frame Respawn",
	         "Bring the recovery position and its collider into Scene view "
	         "without changing the scene, selection, Inspector lock or Undo "
	         "history. The inspected object is used even when Inspector is "
	         "locked. Invalid or missing colliders frame the position marker; "
	         "blocked positions can still be inspected. Fall Below Y is not "
	         "included in the view bounds. The command also works with "
	         "Respawn Guides hidden and opens the Scene tab if necessary."},
	        {"Inspector > Respawn > Select Blocking Colliders",
	         "Select every Box Collider overlapping the inspected object's "
	         "recovery shape, including hidden objects. The character itself "
	         "and face-only contact are excluded. Hierarchy reveals the active "
	         "result. View > Colliders > Selected shows its shape even when "
	         "the model is hidden. Inspector locks are preserved. "
	         "No overlaps leaves the selection unchanged. Invalid collider "
	         "data reports an error without changing selection. Selecting "
	         "does not edit the scene or add history; pending edits finish "
	         "first. Invalid or missing recovery shapes disable the action."},
	        {"Selected Respawn in Scene",
	         "The cross marks the recovery position and the wire box shows "
	         "the character's collider there, including rotation and scale. "
	         "Cyan means the recovery position is above Fall Below Y, has "
	         "valid Character Controller settings and is clear of solid "
	         "colliders; red means it cannot be used. Hidden colliders count "
	         "and the character's own collider is ignored. Face contact is "
	         "allowed. Invalid shapes show only the red position marker. "
	         "The orange plane marks Fall Below Y across the whole scene, "
	         "not only inside the square. These guides check the recovery "
	         "position, not every condition required for Play."},
	        {"Selected Follow Camera in Scene",
	         "The cyan cross marks the target's starting position. "
	         "The cyan line connects it to the Camera. "
	         "The gold cross marks Look Offset; its line shows the view "
	         "direction. "
	         "Follow chains use their resolved starting positions. "
	         "These guides appear only in Scene and do not change the scene."},
	        {"Inspector > Camera > Copy / Paste Lens Settings",
	         "Copy Vertical FOV and Near/Far Clip, then paste onto the "
	         "inspected Camera. A locked Inspector uses its displayed object. "
	         "Primary, Transform, Follow Camera, other components and other "
	         "cameras stay unchanged. Add Component > Paste Camera Lens adds "
	         "a missing Camera with Primary off. Each paste is one Undo/Redo; "
	         "identical settings add no history. The lens copy is independent "
	         "of copied objects, transforms and colliders and survives "
	         "New/Open. "
	         "Pending edits finish first. Editing is unavailable during Play."},
	        {"Edit / Hierarchy > Snap Positions to Grid",
	         "Round each selected object's world position using Scene > Snap "
	         "Settings > Move step (X/Y/Z), even when gizmo snapping is off. "
	         "Halfway values round away from zero. This snaps individual "
	         "origins, so distances between objects may change; model bounds "
	         "and collider faces are not aligned. Rotation, scale and other "
	         "components stay unchanged. Hidden and filtered selections are "
	         "included; selection and Inspector locks are kept. The visual "
	         "grid spacing is separate. One Undo/Redo restores the batch; "
	         "aligned values add no history. Invalid targets cancel all "
	         "changes. "
	         "Pending edits finish first. Unavailable during Play."},
	        {"Edit / Hierarchy > Paste Camera Lenses",
	         "Copy a Camera lens in Inspector, then paste it onto every "
	         "selected object with one Undo/Redo. Missing Cameras are added "
	         "with Primary off; existing Primary flags, Transform, Follow "
	         "Camera, other components, selection and Inspector locks are "
	         "kept. Hidden objects and selected objects outside the Hierarchy "
	         "filter are included. Identical values add no history. Invalid "
	         "targets cancel the entire batch. Pending edits finish first. "
	         "Unavailable during Play or without a copied lens and selection."},
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
