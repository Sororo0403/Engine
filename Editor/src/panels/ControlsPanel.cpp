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
	         "origin when nothing is selected; keep the current distance. "
	         "Camera View and Reset Camera reveal the Scene tab so the "
	         "result is visible even when Game covers it."},
	        {"Hierarchy double-click", "Frame the clicked object. Model-free "
	                                   "objects use their Collider or origin."},
	    });

	DrawSection(
	    "Selection and objects",
	    {
	        {"Hierarchy tree",
	         "Use arrows to fold or unfold child objects. Name sorting applies "
	         "to siblings. Search and component filters show only matching "
	         "objects as a flat list; range selection uses the displayed "
	         "rows."},
	        {"Hierarchy drag and drop",
	         "Drop an object or the selection onto another object to change "
	         "its "
	         "parent. Drop onto Scene Root to unparent. World placement is "
	         "kept "
	         "by default; hold Alt while dropping to keep local Transform "
	         "values. "
	         "A parent cannot be a descendant of its child. Parent changes "
	         "support Undo/Redo."},
	        {"Local Transform",
	         "Inspector values are relative to the parent. Parent movement, "
	         "rotation and scale affect descendants. Copy, duplicate and "
	         "delete "
	         "include descendants. Keeping world placement may be rejected "
	         "when it requires local shear; Keep Local can retain the existing "
	         "local SRT. Copying a branch without its external parent also "
	         "requires a representable root SRT."},
	        {"Hierarchy > Options > Invert Listed",
	         "Toggle selection only for objects matching the current search, "
	         "component, Hidden only and Scene picking filters. Objects "
	         "outside the list keep their selection. Newly selected objects "
	         "are appended in displayed order; the last becomes Active and "
	         "the next Shift-selection anchor. An empty list leaves selection "
	         "unchanged. Available during Play; scene data, picking exclusions "
	         "and Undo history are unchanged."},
	        {"Hierarchy > Options > Sort",
	         "Choose creation order or name A-Z / Z-A. Range selection, Select "
	         "All and arrow keys follow the displayed order; changing sort "
	         "keeps the selection."},

	        {"Inspector > Collider > Shape",
	         "Choose Box, Sphere or Capsule. Radius and cylinder height are "
	         "local dimensions. "
	         "Round shapes require uniform world scale without shear. Shape "
	         "and dimensions support Undo / Redo."},
	        {"Character Controller collider",
	         "Character Controller requires a Collider with Is Trigger off. "
	         "Sphere and Capsule "
	         "are available for static obstacles, Trigger regions and Rigid "
	         "Body objects."},
	        {"Inspector > Add Component > Collider",
	         "Add a collider fitted to the model, edit its local center and "
	         "dimensions, fit again or remove it. Green Scene lines show the "
	         "transformed shape outline. Each drag is one Undo "
	         "operation."},

	        {"Assets folders / Up / Root",
	         "Open a folder or return to its parent or the Assets root. Search "
	         "finds files and folders across Assets. Double-click a folder "
	         "result to open it, then use Back to Search to return. "
	         "Current Folder Only includes the open folder's subfolders. "
	         "Enable All Asset Types to search "
	         "models, textures, scenes and other files together."},
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
	         "Filtering never changes the scene or current selection. "
	         "Primary Camera finds the marked game camera, including invalid "
	         "settings that need repair. Incomplete Mesh / Renderer finds "
	         "missing Mesh, unassigned model paths or missing Mesh Renderers. "
	         "A deliberately hidden renderer is complete. Characters without "
	         "Collider finds unfinished Character Controllers. These filters "
	         "check setup only, not missing asset files; use Scene Check for "
	         "resource validation. All combine with name/ID and Hidden only, "
	         "sorting, range selection and Select All Visible, including "
	         "Play."},
	        {"Assets > Actions > Model Slots",
	         "Choose a slot number from the selected objects' loaded models. "
	         "The table shows source material names and Used counts: how many "
	         "selected objects actually draw with each slot. Mixed Materials "
	         "means different names share that number; Unused means none use "
	         "it. Selecting a row only changes the assignment target number. "
	         "Assign Texture to Selection applies the image through the normal "
	         "Undo/Redo operation. Manual Texture Slot input remains available "
	         "for unused or not-yet-loaded slots. Slot numbers and existing "
	         "assignments are never changed automatically."},
	        {"Inspector > Mesh Renderer > Material Asset",
	         "Assign a shared material to a model slot. New Material saves "
	         "an asset in the Assets folder and assigns it to the slot. "
	         "Save Shared Material updates every slot using the asset. "
	         "Texture Override replaces the base color image for this slot."},
	        {"Inspector > Mesh Renderer > Clear Unused Texture Assignments",
	         "Explicitly remove image assignments whose slot is not used by "
	         "any mesh in the currently loaded model, including old slot "
	         "numbers outside its range. Used images, UVs, visibility and "
	         "other components stay unchanged. A locked Inspector acts on "
	         "its displayed object. One Undo/Redo restores all removed "
	         "assignments. The action requires a loaded Mesh and valid "
	         "Mesh Renderer; no assignments are automatically deleted when "
	         "replacing or reloading a model. Image files are not deleted. "
	         "Unavailable during Play."},
	        {"Inspector > Mesh Renderer > Texture Slots",
	         "Each slot shows the source material name and how many model "
	         "meshes use it. Unused slots keep their original numbers and "
	         "assignments, but changing their image does not affect the "
	         "current "
	         "model. Names are hints; assignments continue to use slot numbers "
	         "after reload or mesh replacement. No image is assigned "
	         "automatically. White remains the default until an image is "
	         "explicitly selected."},
	        {"Inspector > Mesh Renderer > UV",
	         "UV Tiling repeats or mirrors the image; UV Offset moves its "
	         "sampling position. Values apply to all texture slots of this "
	         "object without changing shared images or model geometry. Zero "
	         "tiling samples a fixed coordinate. Each drag is one Undo/Redo "
	         "operation; Reset UV restores tiling 1,1 and offset 0,0. Play "
	         "shows these values read-only. Scene version 13 stores them; "
	         "older scenes load with the default UVs. Game output preserves "
	         "the same settings. Apply UV to Selected Objects copies only "
	         "the inspected UVs to all selected objects as one Undo/Redo "
	         "operation. Every target needs a valid Mesh Renderer; otherwise "
	         "nothing is changed. Images, visibility and other components "
	         "are kept. A locked Inspector can supply UVs from an object "
	         "outside the selection. Copy UV keeps only tiling and offset in "
	         "the Editor clipboard, including across scene changes. Paste UV "
	         "to Selected Objects applies that copy as one Undo/Redo operation "
	         "without changing images. Every selected object must have a "
	         "valid Mesh Renderer; Play remains read-only."},
	        {"Hierarchy > Scene Picking",
	         "Right-click objects and Disable Scene Picking to exclude them "
	         "from Scene clicks and box selection while keeping them visible. "
	         "The current selection is cleared to leave the gizmo safely. "
	         "Picking passes through excluded models and markers. The list "
	         "shows No Scene Pick; Hierarchy selection and editing remain "
	         "available for deliberate changes. Enable Scene Picking restores "
	         "selected objects; Enable All Scene Picking restores all and "
	         "clears the picking-only filter. Scene picking disabled only "
	         "lists excluded objects, combined with name/ID, Hidden only, "
	         "component and sort conditions. Range selection and Select All "
	         "Visible use that same list. Reveal Active clears filter "
	         "conditions while preserving exclusions. These "
	         "workspace flags survive Undo/Redo and Play, but reset on scene "
	         "New/Open/Reload. They do not affect runtime selection, physics, "
	         "rendering, scene files or history."},
	        {"Hierarchy > Options > Hidden only", "List hidden models. Search and range "
	                                    "selection use this filtered list."},
	        {"Edit > Show All Models",
	         "Restore every hidden model in one undoable operation without "
	         "changing selection."},
	        {"Edit > Reveal Active",
	         "Scroll to the active object without changing visibility or "
	         "selection. If it is filtered out, use Restore Filters to return "
	         "to the previous list."},
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
	         "Use Edit to align selected origins to the "
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
	        {"File > Assets Folder",
	         "Choose a folder for browsing and new imports. Use a folder "
	         "outside the Editor build output to retain authored assets "
	         "when rebuilding or cleaning. Use New folder in the Windows "
	         "dialog to create a folder. Cancel "
	         "keeps the current asset source. Existing scene references and "
	         "files are kept in place. New scenes initially save inside an "
	         "absolute Assets folder. Previously selected folders are kept "
	         "for scene export, but only referenced sources are required. "
	         "Folder settings are saved when Editor closes."},
	        {"Assets > Explorer",
	         "Open the current Assets folder in Windows Explorer. The Assets "
	         "list refreshes when you return to Editor. Use Refresh to update "
	         "the list after other external changes. For moves and renames "
	         "that need to update existing references, use the Assets panel."},
	        {"Game Settings > Load Game Configuration",
	         "Open a game configuration file as a draft. Apply adopts its "
	         "settings. Malformed files show an error before applying values. "
	         "Files named *.settings.json are listed in Assets."},
	        {"Game Settings > Save Game Configuration",
	         "Save the valid draft to a game configuration JSON without "
	         "exporting "
	         "a game. Existing destinations require overwrite confirmation. "
	         "Saving does not Apply the draft to Editor; Cancel still keeps "
	         "the earlier settings and leaves an explicitly saved file in "
	         "place. "
	         "Load Game Configuration can reuse the title, dimensions, keys "
	         "lighting and movement space. Scene path and fallback camera "
	         "retain the "
	         "current Editor configuration."},
	        {"File > Recover Scene",
	         "Choose an unsaved scene recorded by an ended Editor session. "
	         "A stable unsaved scene is recorded every 30 seconds with its "
	         "Assets sources and game settings. Play, modal dialogs and active "
	         "edits defer recording. Restore creates a modified Untitled scene "
	         "and offers Save / Don't Save / Cancel for current edits first. "
	         "Original files are never overwritten by recovery. Keep for Later "
	         "or Escape preserves the records. Refresh updates the candidate "
	         "list. "
	         "Delete asks for confirmation. "
	         "Active Editor sessions are excluded. A confirmed save or discard "
	         "clears that scene's record; an unexpected shutdown retains it."},
	        {"File > Save Selection As",
	         "Save selected objects as a reusable *.scene.json in the current "
	         "Assets folder. Save Copy confirms the destination. Placement, "
	         "components and asset references are retained; files are not "
	         "bundled. "
	         "Follow targets outside the selection are cleared. The active "
	         "file "
	         "and its save state are kept; it cannot be overwritten by the "
	         "copy."},
	        {"Assets > Scenes: drag into Scene",
	         "Place a saved object's group on the nearest Collider face or the "
	         "ground. Without a surface, place it along the view ray. The "
	         "group "
	         "uses its combined Collider and visible model bounds; camera-only "
	         "groups use object origins. Relative placement, rotations and "
	         "scales "
	         "are kept. Respawn positions and fall heights move with the "
	         "group. "
	         "All objects form one Undo / Redo operation. Failed placement "
	         "adds "
	         "nothing; the source file is kept unchanged."},
	        {"Assets > Scenes > Add Objects",
	         "Add a saved scene's objects at their saved positions without "
	         "closing "
	         "the current scene. Internal Follow Camera targets receive the "
	         "new "
	         "object IDs, and the existing Primary Camera takes priority. New "
	         "objects are selected for editing together. Undo / Redo affects "
	         "the "
	         "whole addition. No live link is kept to the source file; later "
	         "source edits do not change placed objects."},
	        {"Assets > Asset Type > Scenes",
	         "Browse and search *.scene.json files without loading their "
	         "contents. "
	         "Select Open Scene, double-click a scene, or use its context "
	         "menu. "
	         "Unsaved changes offer Save, Discard or Cancel before opening. "
	         "Invalid or missing files keep the current scene. Opening is "
	         "disabled "
	         "during Play and other modal operations. Save and Save Copy "
	         "refresh "
	         "the list. Older scenes named only *.json still open with File > "
	         "Open."},
	        {"Assets > New Folder",
	         "Create a new folder inside the current Assets folder. Enter a "
	         "single folder name; existing files and folders are never reused "
	         "or overwritten. On success, Assets refreshes, opens the new "
	         "folder and clears the search. Create further folders inside it "
	         "or use it as an image import destination. A failed create keeps "
	         "the dialog and name; Cancel or Escape creates nothing. Folder "
	         "creation does not edit the scene, selection or scene Undo "
	         "history "
	         "and is unavailable during Play, modal operations or Scene "
	         "gestures."},
	        {"Missing or unreadable models",
	         "Open and Reload Scene keep Mesh references even when a model "
	         "cannot load. Scene and the editing Game preview draw the other "
	         "objects. With Object Markers enabled, an unreadable visible "
	         "model has a red origin marker; click or box-select it, or select "
	         "it in Hierarchy. F frames its Collider or origin. Repair the "
	         "reference in Inspector or use Assets > Replace Mesh of "
	         "Selection. "
	         "After restoring the original file, Refresh Assets and Reload "
	         "Model to retry. A successful model cache load also allows retry. "
	         "The missing reference stays in scene saves and Undo/Redo. "
	         "Scene Check lists missing files; Console reports loading errors. "
	         "A previously cached valid model can remain visible. Play checks "
	         "that visible models load before starting; failed Play keeps "
	         "editing. Empty scenes still play. Invalid scene JSON keeps the "
	         "current scene unchanged. Export still requires asset files."},
	        {"Assets > Replace Mesh of Selection",
	         "Replace every selected object's existing Mesh with the chosen "
	         "model in one Undo/Redo operation. Use the model row "
	         "context menu. Every selected object must already have a Mesh; "
	         "a Mesh without an assigned model is supported. Hidden objects "
	         "and Mesh-only objects are included. Names, IDs, Transforms, "
	         "visibility, texture slot numbers, Colliders and gameplay "
	         "settings "
	         "stay unchanged. Fit Colliders again if the new shape needs it. "
	         "Selection and Inspector locks stay unchanged. Failed model "
	         "loading changes no Mesh. Identical assignments add no history. "
	         "Unavailable during Play, modal operations or Scene gestures."},
	        {"Assets > Reload Texture",
	         "Read the selected image again even when its file time and size "
	         "have not changed. Use the asset row context menu. Success "
	         "updates every matching image slot in Scene and Game preview, "
	         "including hidden objects when shown and later Play sessions. "
	         "Scene data, selection, Inspector locks and Undo history stay "
	         "unchanged. Failed reloads keep the last valid texture; fix the "
	         "file and retry. Ordinary file changes still reload "
	         "automatically. "
	         "Unavailable "
	         "during Play, modal operations or Scene gestures."},
	        {"Assets > Reload Model",
	         "Reload the selected model and its linked resources after editing "
	         "files outside Editor. Use the asset row context menu. A "
	         "successful "
	         "reload updates every rendered instance with the same normalized "
	         "model path, including the Game preview and later Play sessions. "
	         "Transforms, names, texture slot numbers, Collider settings, "
	         "selection "
	         "and scene Undo history stay unchanged. Fit Collider or frame the "
	         "model "
	         "again if its new shape needs it. Failed reloads keep the last "
	         "valid "
	         "cached model and existing display; fix the files and retry. "
	         "Refresh "
	         "only updates the Assets list. Reload is blocked during Play, "
	         "modal "
	         "operations and Scene gestures."},

	        {"Assets > Import Model",
	         "Choose a GLB, glTF or OBJ model, an Assets destination and a new "
	         "model folder name. Import copies the model and referenced "
	         "buffers, "
	         "images and OBJ material files, keeping relative paths. Source "
	         "Folder starts at the model's parent. Use Parent Source Folder "
	         "includes sibling dependencies when a model refers to files above "
	         "that folder. All dependencies must remain inside Source Folder "
	         "and use local relative references. Existing folders are never "
	         "overwritten. Failure removes only newly created output and keeps "
	         "the dialog open; cleanup failures report the remaining location. "
	         "Success reveals the copied model in Assets. Add to Scene places "
	         "it afterward; assign images using Mesh Renderer texture slots. "
	         "Import does not edit the scene or its Undo history and is "
	         "blocked "
	         "during Play, modal operations and Scene gestures."},

	        {"Assets > Import Audio",
	         "Choose a WAV, a destination folder and an Audio Name ending in "
	         ".wav. Import validates the full file and copies its original "
	         "bytes without changing the source or overwriting an existing "
	         "asset. Mono and stereo PCM or float32 WAV files are supported. "
	         "The imported clip is revealed in Assets > Audio; assign it in "
	         "the Audio Source Inspector or drag it onto its clip field. "
	         "Importing alone does not modify the scene or its Undo history."},
	        {"Assets > Import Texture",
	         "Choose an external PNG, JPEG, BMP, TIFF or GIF image, an "
	         "existing "
	         "Assets folder and a new image name. Import validates and copies "
	         "the image, refreshes Assets and reveals the result. The source "
	         "and existing files are never overwritten. A failed import keeps "
	         "the dialog open. Cancel creates no asset. The image is not "
	         "assigned automatically; use the texture slot controls afterward. "
	         "Scene data, selection and Undo history stay unchanged. Import "
	         "is unavailable during Play, modal operations or Scene gestures. "
	         "Images must be at most 256 MiB with each dimension at most 16384 "
	         "pixels. Animated images use their first frame for textures."},
	        {"Assets > Asset Type > Textures",
	         "Browse PNG, JPEG, BMP, TIFF and GIF images by folder or search. "
	         "Choose a zero-based Texture Slot, then Assign Texture to "
	         "Selection "
	         "or double-click an image to assign it to every selected Mesh "
	         "Renderer. "
	         "Use Clear Assignment in Inspector to remove an override. "
	         "One Undo restores the entire assignment. Assignment is blocked "
	         "during Play and modal operations."},

	        {"Assets > Add to Scene",
	         "Place the selected model at the world origin."},
	        {"Drag Assets into Scene",
	         "Place a model against the nearest Collider surface and "
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
	         "Add Collider, Character Controller, Camera, Follow Camera or "
	         "Respawn. Changes support Undo and scene saving."},
	        {"View > Colliders",
	         "Show collider outlines for None, Selected or All objects in "
	         "Scene. All includes hidden models and objects without models. "
	         "The outlines match world-axis collision boxes and never appear "
	         "in Game. This display setting is saved with Editor preferences."},
	        {"Create > Camera from Scene View",
	         "Create a fixed Camera at the current Scene view position and "
	         "orientation, vertical FOV and clipping distances. It is "
	         "selected and revealed in Hierarchy in one Undo operation. If no "
	         "Primary exists, the new Camera becomes Primary; otherwise the "
	         "existing Primary is kept. Use "
	         "Inspector > Primary to switch."},
	        {"Create > Empty / Box",
	         "Add a normal object at the world origin and reveal it in "
	         "Hierarchy. Box uses the built-in two-unit model with a matching "
	         "Collider. Scale it into floors, walls or platforms. Creation "
	         "supports Undo/Redo and scene saving. Hierarchy has the same "
	         "creation controls."},
	        {"Hierarchy > Options > Component",
	         "Combine component filters with Search and Hidden only. Models "
	         "without Collider finds model objects missing collision shapes. "
	         "Ctrl+A and range selection use the filtered list. Existing "
	         "selections remain selected when filtered out; Reveal Active "
	         "clears filters to show the active object."},
	        {"Edit > Drop Colliders to Surface",
	         "Move selected Colliders straight down until the first one "
	         "reaches another collider's top surface. Selection spacing is "
	         "preserved; hidden colliders also count. Collision uses actual "
	         "Box, Sphere and Capsule surfaces, with a tiny clearance. All "
	         "selected "
	         "objects need colliders. No surface or an existing overlap leaves "
	         "them unchanged. The action supports Undo/Redo. Drop to Ground still aligns "
	         "visible model bounds to Y=0."},

	        {"Edit > Colliders",
	         "Apply to all selected objects, including hidden ones. Add "
	         "Missing keeps existing collider shapes. Fit All to Models "
	         "replaces or adds shapes; objects without models use a unit box. "
	         "Remove All keeps other components. Each operation supports one "
	         "Undo/Redo; a failed operation changes no colliders."},
	        {"Mesh and Mesh Renderer components",
	         "Mesh stores the model's geometry reference. Mesh Renderer stores "
	         "visibility and texture slot assignments. Add or remove them "
	         "independently from Inspector. Removing Mesh Renderer keeps the "
	         "Mesh reference; removing Mesh keeps the renderer settings. Both "
	         "components and an assigned Mesh are needed to draw. Clear Mesh "
	         "Reference keeps the Mesh component. Asset placement creates "
	         "both. "
	         "Component changes, visibility and texture assignments support "
	         "Undo/Redo. Older scenes are converted on load. Objects with Mesh "
	         "alone can still be selected and framed using their editor "
	         "marker. "
	         "Hierarchy component filters find Mesh and Mesh Renderer "
	         "separately."},

	        {"Mesh Renderer > Texture Slots",
	         "Choose an image directly for each model slot. Model Default "
	         "clears the explicit assignment and restores the model material. "
	         "Assignments "
	         "are saved in the scene and support Undo/Redo. Show Texture in "
	         "Assets "
	         "reveals the image. Unused slots are retained when changing "
	         "models "
	         "and can be cleared individually. Game export includes assigned "
	         "images; no material asset is required. Old scenes migrate the "
	         "image references in their material JSON files; old color, UV and "
	         "shading parameters are no longer used."},
	        {"Scene / Settings save failures",
	         "A save uses a newly created file and replaces the destination "
	         "after writing finishes. An existing staging file is never "
	         "overwritten or removed. A failed replacement keeps the old "
	         "destination and removes only the file created for this attempt. "
	         "Close applications that lock the destination, then retry or "
	         "choose another destination."},
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
	        {"Game > Restart Current Scene",
	         "After a scene transition, restart from the current scene's "
	         "entry point. Shared game data, pause and playback speed carry "
	         "over. Restart returns to the original Play scene."},
	        {"Game > Stop and Select in Editing Scene",
	         "Stop Play and select the same objects in the editing Hierarchy. "
	         "Selection order and the active object are kept. All selected "
	         "objects must exist in both scenes; failure keeps Play and the "
	         "editing selection. Transforms, components, history, files and "
	         "Inspector locks are unchanged. Normal Stop keeps the selection "
	         "from before Play. Unavailable without runtime selection or "
	         "while another dialog is open."},
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


	        {"Inspector > Color Tint",
	         "Add Color Tint from Add Component to multiply the model's "
	         "original material and texture by an RGB color. White restores "
	         "the original appearance. Alpha and UV settings are preserved. "
	         "Color edits, Reset Color and removal support Undo/Redo, scene "
	         "saving, duplication and Play. Objects sharing one model keep "
	         "independent colors. Hierarchy > Options > Component > Color Tint finds "
	         "objects with the component. During Play it is read-only."},
	        {"Edit > Colliders > Paste Colliders",
	         "Apply the copied local collider shape to every selected object, "
	         "including hidden objects and objects without models. Missing "
	         "colliders are added; existing shapes are replaced. All targets "
	         "must accept the shape or none change. Each target keeps its "
	         "Transform and other components. Selection, Inspector locks and "
	         "all clipboards are kept. One Undo/Redo restores the whole edit; "
	         "identical values add no history. Hierarchy has the same action. "
	         "Copy a Collider in Inspector first."},
	        {"Inspector > Add Component > Trigger Actions",
	         "For contact events, add actions to the same object as a Collider "
	         "with Is "
	         "Trigger enabled. Choose Enter or Exit, a target object, and "
	         "Enable, Disable or Toggle. The target's local Active flag "
	         "changes without overwriting its children's flags. Collision "
	         "layers decide which overlaps produce events. Once per Play "
	         "runs that action at most once, even after disabling and "
	         "re-enabling the owner; Start and Restart clear that state. "
	         "Add/remove, target changes, reordering and component removal "
	         "support Undo/Redo. Duplicating a group remaps internal targets. "
	         "Deleting a target clears its reference, which Undo restores. "
	         "Assign all configured targets before Play; an empty list is "
	         "inert."},
	        {"Inspector > Add Component > Audio Source / Audio Listener",
	         "Choose a WAV in Audio Clip, or drag it from Assets > Audio. "
	         "Click the "
	         "clip field to reveal it in Assets. Clear removes the reference; "
	         "Reset "
	         "restores component defaults. Volume and distance drags form one "
	         "Undo "
	         "entry; clip changes, switches, Reset and Remove also support "
	         "Undo/Redo. "
	         "Spatial sources need mono WAV audio; stereo clips can play as 2D "
	         "audio. "
	         "Play on Start runs on activation; Loop repeats the full clip. "
	         "Parent "
	         "transforms position sources and listeners, while attenuation "
	         "distances "
	         "remain in world units. Set Preferred Listener to select it, or call "
	         "scene.set_audio_listener(object) in Lua. With no active listener, "
	         "the rendering camera receives sound. An unassigned source is "
	         "inert. "
	         "Play does not require audio components or an output device."},
	        {"Inspector > Trigger Actions > Play Audio / Stop Audio",
	         "Choose Enter or Exit, then an Audio Source target and Play Audio "
	         "or Stop Audio. Disable Play on Start on event-driven sources. "
	         "Play Audio restarts that source from its beginning; Stop Audio "
	         "also cancels queued playback while no output device is "
	         "available. "
	         "Inactive or unassigned sources are inert. Activity changes apply "
	         "before audio, and the last matching audio operation for a source "
	         "in one simulation update wins. Once per Play resets on Restart. "
	         "Pause does not repeat an operation; Step may queue sound paused "
	         "until Resume. Editor Play and exported games use the same rules. "
	         "Changing action settings supports Undo/Redo."},
	        {"Play audio controls",
	         "Pause freezes owned sound positions; Step keeps audio paused. "
	         "Restart "
	         "starts sound from the beginning and keeps Pause. Focus loss and "
	         "minimization freeze sound, then playback resumes when the app "
	         "returns. "
	         "Natural one-shot completion does not restart every frame. Source "
	         "deactivation stops it; reactivation applies Play on Start again. "
	         "Play Inspector settings are read-only. Scene Check reports "
	         "invalid "
	         "clips and duplicate listeners; game export includes referenced "
	         "WAV files."},
	        {"Inspector > Add Component > Spot Light",
	         "Aim the object's local +Z axis, then set Color, Intensity, Range "
	         "and Inner / Outer Angle. Angles are the full cone in degrees. "
	         "Inside the inner cone the light is full strength; between the "
	         "cones it fades smoothly, and outside the outer cone it is off. "
	         "Equal angles make a hard edge. Range is a world distance. Parent "
	         "rotation and activity apply; scaling does not change the range "
	         "or cone angles. This light does not cast shadows. Up to 32 Spot "
	         "Lights may be active per scene, independently of Point Lights. "
	         "Drags form one Undo entry; Reset, Remove and component creation "
	         "also support Undo/Redo. Play settings are read-only. Saved "
	         "scenes "
	         "and exported games retain the light."},
	        {"Inspector > Add Component > Point Light",
	         "Add a Point Light to an object, then edit Color, Intensity and "
	         "Range. "
	         "Transform sets its position, including parent transforms. Range "
	         "is a "
	         "world distance and does not scale with the object. Light fades "
	         "smoothly "
	         "to zero at Range; Point Lights add colored diffuse light to the "
	         "global "
	         "directional and ambient lighting. Unlit materials ignore all "
	         "lights. "
	         "At most 32 Point Lights may be active at once; Scene Check "
	         "reports "
	         "invalid settings or excess lights. Active and parent Active "
	         "switch "
	         "lights off, including changes from Trigger Actions. Point Lights "
	         "do "
	         "not cast shadows. Edits, Reset and Remove support Undo/Redo; "
	         "settings "
	         "are saved with the scene and included in exported games."},
	        {"Follow Camera > Orbit Input",
	         "Enable Orbit Input on a Follow Camera to turn around its target "
	         "during Play. Focus Game and drag the right mouse button over the "
	         "image, use the wheel to zoom, or use a gamepad's right stick. "
	         "Middle-click or press the right stick to reset by default. Configure "
	         "the bindings in Game Settings > Camera Input. Mouse "
	         "Sensitivity "
	         "is degrees per relative mouse count; Stick Degrees Per Second "
	         "controls gamepad speed. Pitch and Distance limits are applied to "
	         "the initial Camera Offset relative to Look Offset and to input. "
	         "Follow Smoothing affects target motion while orbit rotation and "
	         "zoom respond immediately. Avoid Obstacles shortens the visible "
	         "distance without losing the desired zoom. Only the current "
	         "Primary "
	         "Camera receives input; inactive and other cameras keep their "
	         "state. "
	         "Pause and zero-time updates do not turn the camera. Restart "
	         "resets "
	         "the orbit; Teleport and respawn keep its angle and desired "
	         "distance. "
	         "Settings support Undo/Redo and are saved with the scene; Play "
	         "does "
	         "not change the authored offsets. Exported games use the same "
	         "input."},
	        {"Trigger Actions > Pause Game",
	         "Use Pause Game, Resume Game or Toggle Pause with Button Click "
	         "or Input Pressed. These operations stop gameplay time, motion, "
	         "audio and Lua update while leaving menu input and Lua click "
	         "callbacks available. Use zero Delay on the resume action. "
	         "This pause is separate from the editor's Play controls."},
	        {"Trigger Actions > Input Pressed",
	         "Choose Input Pressed on an active object, then select a named "
	         "action from Game Settings. The press can toggle a menu or run "
	         "another operation anywhere in the scene, including while a "
	         "menu has focus. Use a distinct binding if you do not want "
	         "the same press to also activate a UI Button. Scene Check, Play "
	         "and "
	         "game export report a missing action or one with no binding."},
	        {"Trigger Actions > Restart Scene",
	         "Choose Restart Scene to retry the current level, for example "
	         "from a Button Click action. It resets the scene to the state "
	         "at entry, including the arrival position, and runs Lua start "
	         "again. Lua's shared game table stays available. The restart "
	         "runs on the next simulation step and does not need a target."},
	        {"Trigger Actions > Quit Game",
	         "Choose Quit Game as the operation, for example on a menu "
	         "button with the Button Click event. In the exported game it "
	         "closes the game window. In the editor it stops Play and returns "
	         "to the editing scene."},
	        {"Trigger Actions > Button Click",
	         "On a UI Button, add Trigger Actions and choose Button Click. "
	         "Mouse clicks and keyboard or gamepad submit can then open menus, "
	         "toggle objects, play audio or load another scene. The action "
	         "runs before the button's Lua click callback, if one is assigned. "
	         "A Button Click action needs a UI Button; contact events need a "
	         "trigger Collider."},
	        {"Trigger Actions > Send Script Message",
	         "Choose Send Script Message, select a target with an enabled Lua "
	         "Script, then enter a name and press Enter. The script's "
	         "message(self,state,name,data,sender) receives an empty data "
	         "table "
	         "and the Trigger object as sender. Messages from initial overlaps "
	         "wait for the first simulation update. Inactive targets skip the "
	         "message. Once still applies per Play session."},
	        {"Trigger Actions > Animator",
	         "Choose Play Animator to resume the target's current clip, "
	         "Restart "
	         "Animator to start it from the beginning, Pause Animator to hold "
	         "the current pose, or Stop Animator to restore the bind pose. Set "
	         "the default clip and looping on the target Animator. Enable "
	         "the target before playing or restarting. If several Trigger "
	         "Actions control the same Animator in one update, the last "
	         "matching action wins."},
	        {"Trigger Actions > Emit Particles",
	         "Choose an active Particle Emitter as the target. Each action "
	         "emits its configured Start Burst count at the emitter's final "
	         "position for that update. Turn off Play on Start to use only "
	         "triggered bursts. Existing particles keep their normal "
	         "lifespan."},
	        {"Trigger Actions > Transform Animation",
	         "Choose a target with Transform Animation. Play resumes its "
	         "clock, Restart begins at the first key, Pause holds the current "
	         "pose, and Stop returns to the authored starting pose. Turn off "
	         "Play on Start to wait for a Trigger. The last command for a "
	         "target in an update wins."},
	        {"Trigger Actions > Motion Path",
	         "Play Motion Path moves forward, Reverse Motion Path moves "
	         "backward, and Pause Motion Path holds the current position. "
	         "Clear Play on Start to wait for a Trigger. Switching direction "
	         "keeps the current position. A Once path stops at either end "
	         "and can then run in the opposite direction. Use Enter to open "
	         "and Exit to close a door with one path. If multiple actions "
	         "control the same path in one update, the last one wins."},
	        {"Trigger Actions > First Enter / Last Exit",
	         "Use these events for a pressure plate shared by several objects. "
	         "First Enter fires when the first matching Collider is present; "
	         "Last Exit fires when the last matching Collider is gone. Contact "
	         "and Contact Tag define which objects count for each action. "
	         "These events use the final contacts of a simulation update, so "
	         "a pass-through within one update still uses Enter and Exit."},
	        {"Trigger Actions > Contact",
	         "Choose Any Collider, Character Controller, or Dynamic Rigid "
	         "Body as the contact source for Enter and Exit. A dynamic source "
	         "must have an enabled non-kinematic Rigid Body. Interact is "
	         "always initiated by the character. The contact filter applies "
	         "before Once per Scene, including overlaps at Play start. Contact "
	         "Tag "
	         "adds a case-sensitive match to the selected source type. Assign "
	         "a tag to an object in the scene first, then choose it here."},
	        {"Trigger Actions > Set Camera",
	         "Choose a Camera target to switch the Game view on Enter, Exit "
	         "or Interact. Enter and Exit use any eligible Collider, including "
	         "overlaps at Play start. Both fixed and Follow Cameras can be "
	         "selected. "
	         "The target must be active when the action runs. If several "
	         "actions select a camera in one update, the last one wins. "
	         "Set Blend (s) from 0 to 30 and press Enter. Zero switches "
	         "immediately. A blend eases the position and orientation toward "
	         "the live target pose using simulation time. The target lens "
	         "takes effect at the start. A new target starts from the current "
	         "view; selecting the same target does not restart the blend. "
	         "Restart restores the authored Primary Camera."},
	        {"Trigger Actions > Respawn Character",
	         "For hazards such as spikes, choose Enter and Respawn Character "
	         "on a trigger Collider. Select a character with Respawn. It "
	         "returns "
	         "to its current respawn point, including a checkpoint set in the "
	         "same update. Respawn clears velocity, takes priority over Launch "
	         "Character, and updates the same count as falling below the "
	         "level."},
	        {"Trigger Actions > Launch Character",
	         "For a jump pad, add a trigger Collider and choose Enter and "
	         "Launch Character. Select the player and set the upward speed. "
	         "When using a contact event, only that character can activate "
	         "the action. The launch replaces vertical speed and gravity and "
	         "solid surfaces still apply."},
	        {"Trigger Actions > Set Checkpoint",
	         "Choose a target with Character Controller and Respawn, and a "
	         "destination object. The action runs when that character enters, "
	         "exits or interacts with the Trigger, depending on Event. Falling "
	         "later returns the character to the destination's position at "
	         "activation. Place it on the safe side of the Fall Boundary and clear of solids. "
	         "Restart restores the authored Respawn settings."},
	        {"Trigger Actions > Teleport",
	         "Choose Teleport, set Target to the object to move and "
	         "Destination "
	         "to a marker object. Destination uses its world position; it "
	         "needs "
	         "no Collider and may be inactive. Target keeps its rotation, "
	         "scale "
	         "and parent. Active changes run first, then all destinations are "
	         "sampled before moving any target; the last Teleport for each "
	         "target wins. Moving a character or its ancestor resets character "
	         "velocity and pending jump input, keeps its original respawn "
	         "point "
	         "and snaps affected Follow Cameras. Active Follow Cameras remain "
	         "driven by their Follow settings. Arrival Enter and departure "
	         "Exit "
	         "are reported, but the instant path does not trigger intermediate "
	         "volumes. Resulting actions wait for the next positive update. "
	         "Invalid placement rejects the complete start/update, including "
	         "activity, Once flags and audio requests. Changes to these "
	         "settings "
	         "support Undo/Redo and are included in saved and exported "
	         "scenes."},
	        {"Assets > Prefabs",
	         "Select one root object and choose Create Prefab from Selection "
	         "to save it and all descendants as a .prefab.json asset in the "
	         "current Assets folder. The root's saved position is the origin; "
	         "its rotation, scale, child transforms and component settings are "
	         "retained. External object references must be included under the "
	         "root or cleared first. Models, textures and WAVs remain shared "
	         "file references relative to the Prefab, so keep their folders "
	         "together. Existing assets are not overwritten. Choose Prefabs "
	         "in Asset Type, select an asset and Add Prefab, or drag it onto "
	         "Scene to fit the group to a surface or grid. Placement remaps "
	         "internal references, validates resources and creates one "
	         "Undo/Redo "
	         "operation. New placements retain their source asset and original "
	         "IDs. "
	         "Saved scenes and exported games contain the placed objects "
	         "and their ordinary resource dependencies."},
	        {"Inspector > Prefab Instance",
	         "Refresh from Prefab keeps individual edits and local additions "
	         "or deletions, and inherits source fields you have not changed. "
	         "Arrays are overridden as one value. Refresh All Prefab Instances "
	         "updates matching placements in one Undo operation. Revert Prefab "
	         "Overrides restores source fields, deleted members and hierarchy, "
	         "and removes local additions. The root Transform and parent are "
	         "always retained. Save to Prefab Asset saves non-placement values "
	         "and the current hierarchy, then refreshes matching instances. "
	         "External asset changes must be refreshed before saving. Nested "
	         "Prefabs must be unpacked before saving a containing asset. "
	         "Unpack Prefab removes the link and keeps the objects; Undo "
	         "restores "
	         "the link. Scene Undo does not undo the asset file save. Missing "
	         "assets keep the current objects usable; Unpack is still "
	         "available. "
	         "Duplicating a whole instance retains its link, while copying "
	         "only "
	         "a member creates an independent object. Scene saves retain "
	         "links; "
	         "Play and game export use ordinary object data without source "
	         "links."},
	        {"Trigger Actions > Interact",
	         "Set Event to Interact to operate a switch or door on a button "
	         "press instead of immediately on entering. The active Character "
	         "Controller must currently overlap this object's trigger Collider "
	         "and both collision masks must permit the pair. Press the "
	         "Interact "
	         "key (E by default) or the configured gamepad Interact button. "
	         "Game Settings can reassign both. To use a named action, add it in Game "
	         "Settings "
	         "and select it under Input. An unavailable named action will not "
	         "fire. Holding an input does not repeat. No Character is "
	         "needed "
	         "to start Play, but interaction has no receiver without one. "
	         "All overlapped receivers run in ID order, after queued and new "
	         "Enter/Exit signals. Interact supports the same operations and "
	         "Once "
	         "per Play as Enter/Exit. Pause and zero-time updates do not "
	         "interact; "
	         "physical Trigger history continues to show contact events only. "
	         "Interact settings support Undo/Redo, save and game export."},
	        {"Trigger Actions > Delay",
	         "Enter a delay in seconds (0 to 600) and press Enter. A zero "
	         "delay runs immediately. A positive delay starts when the event "
	         "matches and advances only during simulation, including Step. "
	         "Leaving the trigger does not cancel the action. The trigger "
	         "must still be active when the delay ends. Once reserves only "
	         "one action per Play; restarting or changing scenes clears "
	         "pending actions."},
	        {"Trigger Actions execution",
	         "Initial overlaps run at Start. Later actions run after a "
	         "positive simulation update: queued signals first, then new "
	         "Trigger events in pair ID order, with each pair's owners in "
	         "ID order and actions in list order. Conflicting writes use "
	         "that order; two "
	         "Toggles cancel. Stay does not run actions. Activity changes are "
	         "applied as one batch. Events caused by that batch are visible "
	         "in Play State immediately, while their actions wait for the "
	         "next positive update. Disabled receivers do not receive new "
	         "signals. Zero-time updates and Pause do not execute queued "
	         "actions. Invalid changes, such as enabling a wall inside a "
	         "character or blocking its recovery point, reject the entire "
	         "start/update and preserve the prior runtime state. The error "
	         "identifies the problem; the editing scene is always kept."},
	        {"Inspector > Active",
	         "Enable or disable an object's drawing, collision, Trigger "
	         "detection and gameplay components. The object also needs every "
	         "ancestor enabled: Active in Hierarchy shows that effective "
	         "state. Disabling a parent keeps each child's own checkbox. "
	         "Inactive objects remain editable in Hierarchy and Inspector; "
	         "Scene clicks and rectangle selection skip them. Explicit frame "
	         "requests can use their Collider or origin, and selected debug "
	         "outlines remain editing aids. Copy, duplicate "
	         "and saved scenes retain the flags. Older scenes start enabled. "
	         "Play works without an active Character or Camera; inactive "
	         "gameplay components do not block it. Active Follow Cameras may "
	         "read the frozen transform of an inactive target. An inactive "
	         "Camera cannot be pinned or chosen for Play. Stop keeps the "
	         "authored flags, and object activity is read-only during Play."},
	        {"Inspector > Collider > Is Trigger",
	         "Make a Collider detect overlaps without blocking movement, "
	         "grounding, respawn or Follow Camera obstacle avoidance. "
	         "Both collision masks must permit the other layer. A Character "
	         "Controller itself needs a non-trigger Collider; put extra "
	         "detection volumes on other objects or children. Geometry "
	         "selection includes Trigger shapes; dropping ignores Trigger "
	         "supports. The flag "
	         "supports Undo/Redo, copy/paste and duplication; fitting the "
	         "shape keeps it. Older scenes load as solid Colliders."},
	        {"View > Play State > Triggers",
	         "Inspect active Trigger contacts and the latest 128 Enter/Exit "
	         "events, including object IDs, simulation frames and game time. "
	         "Click an object to select it; double-click to frame it. Start "
	         "and Restart record initial overlaps at frame 0, while Pause "
	         "retains the display and Step advances it. Stop clears events. "
	         "Stay is available to runtime code each positive update but does "
	         "not fill the history. Face-only touches do not count. A volume "
	         "crossed between updates produces Enter then Exit from linear "
	         "relative translation of unchanged shapes; curved motion, size "
	         "changes and "
	         "intermediate rotation are not reconstructed. Respawn teleports "
	         "test their endpoints without crossing the intervening space. "
	         "No gameplay component is needed to start Play or use Triggers."},
	        {"Inspector > Collider > Collision Layer / Collides With",
	         "Choose one of 32 layers per Collider (0 is Default), then check "
	         "the layers it can collide with. Both Colliders must allow the "
	         "other's layer; disabling either side prevents contact. Nothing "
	         "allows passage through every Collider. Character movement, "
	         "grounding, jump prediction and respawn checks use these same "
	         "rules in Play and exported games. Filters do not change drawing "
	         "or selection, do not inherit from parents, and do not hide "
	         "geometry from placement or Follow Camera obstacle queries. "
	         "Each choice supports Undo/Redo; copy/paste and duplication "
	         "include the filter, while Fit Collider keeps it. Older scenes "
	         "use Default and Everything until edited."},
	        {"Inspector > Collider > Collider Size",
	         "Enter the full local X/Y/Z dimensions. The displayed Size is "
	         "twice the internal half extents; Transform scale and rotation "
	         "still affect the world collision bounds. Center stays unchanged. "
	         "A continuous drag is one Undo/Redo operation. Play displays the "
	         "same full dimensions read-only. Existing scene files and copied "
	         "collider data keep their original dimensions."},
	        {"Inspector > Collider > Copy / Paste Collider",
	         "Copy the local Center, Size and collision filter onto the "
	         "inspected object. Add Component > Paste Collider adds a "
	         "missing component. The target's Transform, other components, "
	         "selection and Inspector lock stay unchanged; target rotation and "
	         "scale still determine the world collision box. Paste supports "
	         "one Undo/Redo; identical values add no history. Invalid target "
	         "bounds reject the paste. Object and Transform clipboards are "
	         "independent and are kept. The collider copy also survives scene "
	         "changes. These editing actions are unavailable during Play."},
	        {"Inspector > Collider > Frame Collider",
	         "Bring the inspected collider's actual collision box into Scene "
	         "view, including its center offset, rotation and scale. Models "
	         "may be hidden or absent; a locked Inspector uses its fixed "
	         "object. The Scene tab opens if necessary. Selection, collider "
	         "display, object data and Undo history stay unchanged. View > "
	         "Colliders > All shows hidden or unselected shapes. Invalid "
	         "colliders disable the action. Bounds outside the camera's "
	         "clipping range leave the camera unchanged."},

	        {"Game while editing",
	         "Preview the authored scene from the Primary Camera without "
	         "running physics. Follow Cameras show their starting poses. "
	         "Without a Primary, use the Scene view position. The image uses "
	         "Game Settings resolution and aspect. Invalid camera chains clear "
	         "the preview; incomplete movement/collider settings do not "
	         "require Play."},
	        {"UI Image > Fill",
	         "Use Fill Amount from 0 to 1 and choose a Fill Origin to make a "
	         "health or progress bar. Fill crops the image from the chosen "
	         "edge, keeping the texture scale and Rect Transform. The pointer "
	         "hit area stays the full rectangle; clear Image Raycast Target "
	         "for a decorative gauge. Lua can read or change it with "
	         "get_image_fill() and set_image_fill(amount)."},
	        {"Scene Check > Copy Results",
	         "Copy the last completed check's issue count and every message "
	         "as text. Zero issues can be copied too. Before the first check "
	         "there is nothing to copy. If checking itself fails, copy its "
	         "failure reason instead of an older result. Copying does not "
	         "rerun checks or change the scene, selection or Undo history. "
	         "Run Check Scene again after editing to update the results."},
	        {"View > Scene Check",
	         "Check the current editing scene without starting Play. Results "
	         "list component, follow-camera, start-position and recovery-point "
	         "issues; Select Object reveals a result in Hierarchy and keeps "
	         "Inspector locks. Inspect Object explicitly unlocks Inspector "
	         "and opens that issue object's settings. It finishes pending "
	         "edits without creating a new scene edit. Frame Object also "
	         "brings its location into "
	         "Scene. "
	         "Select Listed Issue Objects selects each matching existing issue "
	         "object "
	         "once; "
	         "Frame Listed Issue Objects also fits their combined bounds. "
	         "Search Issues filters by message, current object name or ID "
	         "(case-sensitive); Clear Search restores all results. Copy "
	         "Results always includes all results. Deleted "
	         "objects and issues without an object are excluded. Hidden or "
	         "missing models use their Collider or origin if model bounds are "
	         "unavailable. Invalid bounds leave the camera unchanged. These "
	         "operations do not change scene data, visibility or undo history. "
	         "Results describe the last check: use Check "
	         "Scene "
	         "after edits. Fix invalid prerequisites before rechecking "
	         "dependent "
	         "collision rules. Empty scenes are valid. This checks playback "
	         "settings plus model/image loading and dependency-file "
	         "existence. "
	         "Hidden meshes, renderer-only texture assignments and unused "
	         "slots are included. Model dependencies include OBJ material "
	         "files and glTF buffers and images. Shared file results are "
	         "reused during each check. Explicit texture assignments are "
	         "decoded using the same first-frame, dimension and file-size "
	         "checks as Import Image, including hidden objects and unused "
	         "slots. GPU rendering and "
	         "export-folder restrictions are not checked. Scene data and Undo "
	         "history are preserved."},
	        {"Game > Play / Stop",
	         "Run the current scene, including empty scenes. Stop returns to "
	         "editing and keeps your changes and Undo history."},
	        {"Game > Play Startup Scene",
	         "Run the project's startup scene while keeping the current scene "
	         "open for editing after Stop."},
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
	        {"Game > Start Paused",
	         "Start new Play sessions paused after scene initialization. "
	         "Use Step to advance one frame or Resume to run normally. "
	         "This choice is saved for the next Editor session. Restart "
	         "keeps the current pause state."},
	        {"Game > Load Test Save",
	         "Start Play with the test save written by Run Game in the export "
	         "result. In-Editor Play keeps changes in memory. Restart Play "
	         "returns to the data loaded when Play began. If the save cannot "
	         "be read, fix the file and retry, or choose Play Without Test Save."},
	        {"Game > Maximize on Play",
	         "While stopped, enable this option to expand Game automatically "
	         "when a new Play session starts. The choice is saved for the "
	         "next Editor session. Restoring the layout during Play keeps it "
	         "restored; Restart keeps the current view. Stop restores the "
	         "layout, and the next Play follows this option again. Failed "
	         "Play validation does not expand Game."},
	        {"Game > Maximize Game / Restore Layout",
	         "Expand Game across the Editor work area during Play. The "
	         "original dock arrangement and render resolution are kept. "
	         "Restore Layout returns to the panels without restarting or "
	         "changing Pause, playback speed or selection. Stop and Escape "
	         "restore the normal layout. Restart keeps the expanded view. "
	         "Play shortcuts remain available; other panels are hidden until "
	         "the layout is restored. Opening a panel, Grid Settings or Help, "
	         "or framing Scene from the menu restores the layout immediately "
	         "and shows that view."},

	        {"View > Play State",
	         "Inspect the live character position, vertical velocity and "
	         "grounded state, plus the rendered camera pose and lens. "
	         "Copy Summary copies the active scene, playback state and shared "
	         "Lua data for debugging. "
	         "Respawns counts successful fall recoveries; Last Respawn shows "
	         "the most recent recovery position. Both reset on Start, Restart "
	         "and Stop, and are unchanged by failed updates. The "
	         "panel is read-only and can be opened during Play. Pause freezes "
	         "the values; Step shows the next simulated frame. Stop clears "
	         "the live display. Simulated Time accumulates only successful "
	         "game updates after speed scaling and the frame-time cap. "
	         "Simulation Frames counts positive-time updates, not rendered "
	         "frames or internal physics ticks. Pause and zero-time updates "
	         "do not advance these values; Step adds one frame and 1/60 "
	         "second at any speed. Start, Restart and Stop reset both. "
	         "Ground Collider shows the supporting object while grounded and "
	         "None while airborne. Select Ground Collider reveals that live "
	         "object; Frame Ground Collider also fits it in Scene. Hidden "
	         "Colliders are included. Select Character / Camera reveals "
	         "the live object in Hierarchy; "
	         "Frame also moves the Scene camera to it, including hidden or "
	         "model-free objects. Camera selection is unavailable for a fixed "
	         "view without a Primary Camera. Dialogs disable these actions. "
	         "Simulation, editing selection, Inspector locks, scene data and "
	         "history are unchanged."},
	        {"Inspector > Camera > Play From This Camera",
	         "Test the displayed Camera without changing the editing Primary "
	         "flags, history or files. The selected Camera becomes Primary "
	         "only in the Play snapshot; Follow Camera behavior and its lens "
	         "are preserved. Restart keeps this choice. Stop discards it; "
	         "normal Play and exported games use the authored Primary. The "
	         "request starts next frame and is cancelled by another action "
	         "or a dialog. Invalid scene settings still prevent Play."},
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

	        {"View > Camera Guides",
	         "Toggle the frustums of selected cameras in the Scene view. This "
	         "setting is saved for the next Editor session."},
	        {"Selected Camera in Scene",
	         "The gold wireframe shows Near/Far Clip and the field of view "
	         "at the aspect ratio in Game Settings. Follow Camera previews "
	         "its starting view using the current target position."},

	        {"Edit > Snap Positions to Grid",
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

	        {"Inspector > Camera > Reset Lens Settings",
	         "Restore the default FOV and Near/Far Clip in one Undo operation. "
	         "Primary, Transform and Follow Camera settings stay unchanged. "
	         "Pending edits finish first. Default values add no history."},






	        {"Inspector > Character Controller > Step Height",
	         "Set Step Height above zero to walk onto small steps while "
	         "grounded. The controller checks clearance above and ahead, "
	         "and needs a walkable surface to land on. Zero keeps automatic "
	         "step-up disabled."},
	        {"Inspector > Character Controller > Ground Snap",
	         "Set Ground Snap Distance above zero to stay grounded on shallow "
	         "descents or small drops. After a grounded move, the controller "
	         "looks down for a walkable floor within this distance. Jumps and "
	         "larger drops still leave the ground. The default is zero."},
	        {"Inspector > Character Controller > Jump Grace",
	         "Coyote Time allows jumping shortly after leaving a platform. "
	         "Jump Buffer Time keeps a press made before landing and consumes "
	         "it once on contact. Set seconds from 0 to 1; 0 disables that "
	         "grace. Both default to 0, including scenes saved before version "
	         "10. Try 0.1 seconds while tuning a platforming stage. Timers "
	         "advance with the 120 Hz simulation, pause with Play and reset "
	         "on Restart or respawn. A jump consumes the ground grace, so it "
	         "does not enable double jumping. Edits support Undo, copying, "
	         "scene saving and game export. Play still requires a Collider "
	         "for a Character Controller."},

	        {"Inspector > Reset Character / Follow / Respawn Settings",
	         "Reset this component to defaults in one Undo operation. "
	         "Follow Camera keeps its target; offsets and smoothing reset. "
	         "Respawn uses the start position and default fall height. "
	         "Transform and other components stay unchanged. "
	         "Pending edits finish first. Default values add no history."},


	        {"Inspector > Follow Camera > Avoid Obstacles",
	         "Keep the camera before the first static Collider between "
	         "its look point and smoothed position. Camera Clearance is the "
	         "world-axis-aligned box half size around that path, from 0.01 to "
	         "5 units. Hidden Colliders block it too. The followed target, "
	         "Character Controllers and Follow Cameras are excluded. The "
	         "camera pulls inward immediately, then uses Follow Smoothing to "
	         "return outward. If the look point's clearance enters a Collider "
	         "during Play, keep the last safe camera position and continue "
	         "tracking "
	         "the look point until the path clears. An invalid initial "
	         "clearance "
	         "prevents Play; adjust "
	         "Look Offset or Camera Clearance. Scene guides, Game preview and "
	         "exported games use the same rule. Enabled and clearance edits "
	         "support Undo/Redo, copy/paste and scene saving. Off by default, "
	         "including scenes saved before version 12."},
	        {"Inspector > Follow Camera",
	         "Offsets use world coordinates. Follow Smoothing 0 follows "
	         "immediately. Choose the target object in the same scene; IDs "
	         "distinguish matching names. None clears the target. "
	         "Self-reference, "
	         "circular chains and broken target chains cannot be selected. "
	         "Target changes support Undo/Redo."},

	        {"Inspector > Follow Camera > Select Target",
	         "Select only the referenced object and reveal it in Hierarchy. "
	         "This can clear Hierarchy filters and keeps hidden models hidden. "
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
	        {"Gamepad (Game focused)",
	         "Enable Gamepad in File > Game Settings controls both Play and "
	         "exported games and is saved with game configurations and "
	         "Editor preferences. Older game configurations enable it by "
	         "default. Play captures the option at start; changing settings "
	         "applies to the next Play. When disabled, gameplay uses only "
	         "the configured keyboard keys. "
	         "Game Settings can choose the move stick, D-pad, Jump and Interact "
	         "bindings. Stick strength controls "
	         "movement speed. Keyboard and gamepad input combine without "
	         "exceeding the Character Controller move speed. The same input "
	         "works in exported games and uses Camera Relative Movement "
	         "when enabled. Scene navigation, text fields and dialogs block "
	         "gamepad gameplay input in the Editor; losing application "
	         "focus blocks it in both Editor and Runtime."},
	        {"Gameplay Keys (default WASD / Space)",
	         "Click the Game image to focus it. Use the keys assigned in File "
	         "> Game Settings to move and jump when the object "
	         "has a Character Controller."},
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
	         "Confirm to reload the active file from disk. Save Copy and Reload "
	         "preserves unsaved edits in a separate file before reloading. "
	         "Reload clears undo history; a failed load keeps the edited scene."},
	        {"File > Game Settings > Camera Relative Movement",
	         "Move forward along the rendered Game camera's horizontal look "
	         "direction, and right perpendicular to it on world X/Z. Uses the "
	         "current Primary Camera, including Follow Camera, or the fixed "
	         "fallback camera when no Primary exists. Pitch and roll do not "
	         "tilt movement. A near-vertical camera falls back to world axes. "
	         "Off by default, including old game configurations. Applies to "
	         "Editor Play and exported games with the same keys and focus "
	         "guards. Jump remains vertical. Apply and Cancel keep scene data "
	         "and history unchanged."},
	        {"File > Game Settings > Lighting",
	         "Set the directional light's travel direction, color, intensity "
	         "and ambient brightness. Direction must be finite and nonzero. "
	         "Color and ambient use 0-1; intensity uses 0-16. Reset Lighting "
	         "restores defaults. Apply updates Scene and Game previews; Cancel "
	         "keeps applied values. Play and Restart retain the lighting from "
	         "Play start. Settings are retained across Editor sessions and "
	         "included in saved configurations, recovery and exported games."},
	        {"File > Game Settings",
	         "Set the game title, render size and five gameplay keys before "
	         "Play or export. Choose distinct letters, digits, Space or arrow "
	         "keys for movement and jumping. Reset Gameplay Input restores "
	         "WASD and Space. Apply validates all fields; Cancel keeps the "
	         "previous settings. Scene data and undo history stay unchanged. "
	         "Play and exported games share these bindings. Game focus and "
	         "text-input guards still apply. Assign Escape to a named action "
	         "to "
	         "use it in the game, for example for Toggle Pause. Without an "
	         "assignment, Escape stops Play in Editor. Exported games can "
	         "bind Escape to a Quit action. "
	         "Settings are saved when Editor closes. Load Game Configuration "
	         "reads an exported game.json into this draft; Apply accepts its "
	         "title, size, keys and movement basis. Cancel keeps the previous "
	         "settings. The active scene and camera view are kept. Old "
	         "configurations use WASD and Space."},
	        {"File > Project Scenes",
	         "Open a scene from the project by its path relative to the scenes "
	         "folder. Switching scenes prompts you to save any pending edits."},
	        {"File > Import Game Scene",
	         "Choose an exported game's game.json and a new Assets folder. "
	         "Import its startup scene and referenced models, buffers and "
	         "images as a new unsaved scene. Existing Assets folders and "
	         "the source package are kept; failed copies remove only their "
	         "own files. Current scene changes require save confirmation. "
	         "Game settings are kept by default. Enable Import Game Settings "
	         "to adopt the title, dimensions, gameplay keys, movement basis "
	         "and lighting from that package after the whole import succeeds. "
	         "The current export scene path and fallback camera metadata are "
	         "kept; cancel or failure keeps all current game settings. "
	         "Explicit texture assignments are decoded before and after "
	         "copying, including hidden objects and unused slots. Invalid "
	         "images leave the scene, game settings and existing assets "
	         "unchanged and clean up newly created import output. "
	         "The option resets to off each time. Save As chooses a "
	         "new scene file; import does not add the package to Open Recent."},
	        {"File > Export Game",
	         "Choose a new folder, game title and window size. Build Release "
	         "first. Runtime must report the same game configuration and scene "
	         "format versions as the Editor. Missing, malformed or mismatched "
	         "build information asks you to rebuild Release Runtime before "
	         "creating output. A missing Runtime output folder also asks "
	         "you to build Release Runtime. Export keeps the active scene "
	         "and saved "
	         "state. Explicit "
	         "texture assignments are decoded before and after copying, "
	         "including hidden objects and unused slots. Invalid images "
	         "reject the export; only newly created output is cleaned up. "
	         "After "
	         "success, "
	         "Open Output Folder opens the package and Run Game starts its "
	         "Game.exe independently. Close returns to editing. Failed open "
	         "or launch requests keep the result screen available for retry. "
	         "The exported game can also run directly from its folder."},
	        {"File > Return to Export Check",
	         "After opening a reported scene or asset, return to the same "
	         "export settings and check again. Use Save Scene and Check to "
	         "include unsaved changes to a linked scene."},
	        {"Export Result > Source Scenes",
	         "Open a scene from the current project's export in the Editor. "
	         "Use File > Return to Export Result to see the same logs, or "
	         "File > New Export from Result to prepare a new output folder "
	         "with the previous window settings."},
	        {"Export Result > Switch Export",
	         "After exporting again from a result, switch between the two "
	         "output folders. Each keeps its own log view. Use Run Without "
	         "Save Data to try a fresh run or to compare while the other "
	         "export is using the same test save."},
	        {"File > Save Copy", "Save a separate scene file and keep the "
	                             "active file and saved state."},
	        {"Ctrl + Z", "Undo the last edit."},
	        {"Ctrl + Y / Ctrl + Shift + Z", "Redo an edit."},
	        {"View > History", "Inspect operations, current and saved states. "
	                           "Click a state to undo or redo to that point."},
	        {"View > Console",
	         "Filter recent logs by level or text, read details, copy a "
	         "message or copy all matching logs with timestamps and levels. "
	         "Latest Play Run shows messages from the most recent Play or "
	         "Restart attempt onward. "
	         "Clear Console resets recent messages while file output is kept."},
	        {"Windows file picker",
	         "Open, Save, Save Copy and folder selection use the Windows "
	         "dialog. Use Search project scenes to filter the project scene "
	         "list in Editor. Overwrite confirmation names the chosen destination."},
	        {"File > Open Recent", "Reopen a recently used scene, with a save "
	                               "prompt for unsaved changes."},
	        {"File > Open Previous Scene (Ctrl+Shift+O)",
	         "Open the most recent other scene in the current project's scenes "
	         "folder. With no project open, use the full scene history. "
	         "Hover to see the path before opening it."},
	    });

	DrawSection(
	    "Editor settings",
	    {

	        {"View > Camera Settings",
	         "Adjust movement speed, acceleration and mouse sensitivity. "
	         "Scene Projection controls the Scene view's vertical FOV and "
	         "Near/Far Clip, including the observation view during Play. "
	         "Increase Far Clip to see and frame larger stages or drafts. "
	         "Near Clip must be positive and below Far Clip. Invalid changes "
	         "keep the previous projection. Reset Scene Projection restores "
	         "only this lens. These Editor settings are saved across sessions "
	         "and do not change Camera components, Game rendering, game "
	         "output, "
	         "scene data or Undo history."},



	        {"View > Grid Settings",
	         "Adjust grid spacing, extent, major-line interval and work-plane "
	         "Height. Model and saved-object-group drops use that height when "
	         "no Collider surface is hit, even if grid lines are hidden. "
	         "Collider faces still take priority. A parallel ray or a plane "
	         "outside the visible ray range keeps the existing near-ray "
	         "placement. These are Editor settings, saved across sessions; "
	         "they do not change scene objects, physics or game output. "
	         "Drop to Ground still uses world Y=0; position snapping uses its "
	         "existing world-origin intervals. Restore Defaults resets the "
	         "height to zero."},
	        {"Load Scene > Arrival Tag",
	         "Set a unique tag on an active marker in the destination scene. "
	         "Choose Arrival Tag lists each unique active tag with its object "
	         "name and ID from the saved destination. You can also type a tag "
	         "and press Enter. The destination "
	         "must have an active Character Controller. Preferred Player Character "
	         "takes priority. Call scene.set_input_character(object) to switch "
	         "during Play. The marker must "
	         "leave space for its Collider. The character keeps its authored "
	         "orientation. An empty tag uses the authored position. Check "
	         "Destination verifies the saved scene with either placement. "
	         "Scene Check reports invalid tagged arrivals on the source Trigger. Game "
	         "Export validates tagged arrivals throughout its scene graph."},
	        {"Load Scene",
	         "Use Trigger Actions > Load Scene to choose a saved scene from "
	         "Assets or drop a scene asset. Enter, Exit and Interact "
	         "conditions use the existing Trigger rules. This operation needs "
	         "no object target. The last request in action order is used; "
	         "loading runs on the next positive game update, once per frame, "
	         "so cycles cannot recurse. Pause holds requests; Step may load "
	         "one scene. Missing or invalid scene data is reported instead of "
	         "opening a partial scene. Restart returns to the Play-start "
	         "snapshot, and Stop leaves the editing document unchanged. "
	         "Selection, trails and audio are reset on scene changes; carrying "
	         "runtime Transforms back is available only in the initial scene. "
	         "Game output includes every configured reachable scene and its "
	         "assets, including inactive references and cycles, up to 256 "
	         "scenes. Imported games retain the same links."},
	        {"Motion Path > Waypoint Wait",
	         "Set Waypoint Wait (s) to hold at each point reached, for example "
	         "to give players time to board a lift. The path starts moving "
	         "immediately. Loop and Ping Pong also hold when they return "
	         "to the starting point. Zero keeps continuous movement."},
	        {"Motion Path",
	         "Add Motion Path in Inspector to move an object along offsets "
	         "from its Play-start local position. The first point stays at "
	         "zero. Each segment uses Segment Seconds; choose Once, Loop "
	         "(including the return segment) or Ping Pong, with optional "
	         "Smooth easing. Colliders create moving floors: standing "
	         "characters ride along. Solid paths stop at obstacles or when a "
	         "passenger would be crushed, and resume when clear. Trigger and "
	         "model-only paths do not block. Inactive branches pause their "
	         "path; Pause and Step use game time. Character Controller and "
	         "Follow Camera cannot share the moving object. Child objects "
	         "inherit motion. Restart restores the authored position. All "
	         "settings and point edits support Undo/Redo and game output."},
	        {"Rigid Bodies",
	         "Add Rigid Body and a non-Trigger Collider on the same "
	         "object. "
	         "Mass, friction, restitution, gravity, damping, initial "
	         "velocities "
	         "and world-axis rotation locks are edited in Inspector. Positive "
	         "world scale without shear and positive uniform ancestor scales "
	         "are required. "
	         "Use the body's own scale for non-uniform dimensions. Dynamic "
	         "bodies keep their "
	         "world pose when parents move. Motion Path on the same object "
	         "requires Is Kinematic. A solid child Collider needs its own "
	         "Rigid "
	         "Body; compound shapes are not supported yet. The current "
	         "physical "
	         "shape is a box. Up to 1024 solid Colliders are supported in a "
	         "physics scene. Character Controller keeps its own movement and "
	         "gravity settings; it cannot share the object with Rigid Body. "
	         "Pause keeps the physical state, Step advances once and Restart "
	         "restores authored values. Settings, Prefabs and game output "
	         "preserve Rigid Body components."},
	        {"Rigid Body Scripts",
	         "Use get_velocity/set_velocity(x,y,z) in world m/s and "
	         "get_angular_velocity/set_angular_velocity(x,y,z) in world rad/s. "
	         "Each setter keeps the other velocity unchanged and respects "
	         "rotation locks. add_impulse(x,y,z) applies an instantaneous "
	         "world-space impulse at the center of mass; mass affects the "
	         "result. Speeds are capped at 500 m/s and 100 rad/s. Use "
	         "is_sleeping() for the current state and is_dynamic() to safely "
	         "filter objects before applying impulses. Only active, enabled "
	         "Rigid Bodies have runtime velocity; impulses require Dynamic. "
	         "Motion Path owns its body's velocity. Kinematic without Motion "
	         "Path accepts velocity setters. Named references and Trigger "
	         "other objects use the same API. Mutations belong inside "
	         "callbacks. "
	         "Getters immediately see prior writes; motion advances on the "
	         "next "
	         "positive physical step. Position/active/scale changes are synced "
	         "before the next physical API. Errors restore scene, Lua state "
	         "and physics together. A callback allows 128 physical API calls; "
	         "a game step shares 1024. Pause holds state and Restart restores "
	         "the authored initial velocities. These calls never edit the "
	         "Inspector's initial values."},
	        {"Lua Scripts",
	         "Create Script in Assets, then add Lua Script in Inspector and "
	         "choose its asset. Edit Script opens the built-in code editor. "
	         "Save checks syntax without running code; asset saves are "
	         "independent of scene Undo. Changes take effect at Play / "
	         "Restart. A script returns a table with start(self,state), "
	         "update(self,state,seconds,input), "
	         "trigger_enter/trigger_stay/trigger_exit(self,state,other). "
	         "Module locals reset for every callback; store persistent "
	         "per-object values in state, a JSON object of finite numbers, "
	         "booleans, strings and nested tables. Apply Initial State records "
	         "one scene undo step. Input exposes moveX, moveZ, jumpPressed and "
	         "interactPressed. self:get_position()/set_position(x,y,z) use "
	         "world coordinates; get_local_position/set_local_position, "
	         "get_rotation/set_rotation(x,y,z,w) and get_scale/set_scale use "
	         "local values. Position changes are teleports; use Character "
	         "Controller or Motion Path for collision movement. Use "
	         "get_name/set_name, is_active/set_active, parent(), get_id(), "
	         "reference(name), scene.find(name), scene.find_id(id) and "
	         "scene.log(text). Named references follow copy, delete Undo and "
	         "Prefabs. World position on a child may fail if its transform "
	         "cannot be represented without shear. Script errors roll back the "
	         "whole game step. Start runs once; inactive branches pause; Pause "
	         "holds state and Step advances once. Trigger contacts caused by "
	         "scripts are delivered on the next step. Scripts cannot access "
	         "files or load modules; memory, execution, state size and "
	         "callback count are limited. Game output includes assigned Lua "
	         "assets in every reachable scene."},
	        {"Game UI",
	         "Create > UI adds a Canvas, Text or Image. Text/Image creation "
	         "uses the selected Canvas branch or creates a Canvas when "
	         "necessary, as one undo step. Edit the result in Inspector and "
	         "preview it in Game. Canvas scales from its Reference Resolution; "
	         "Match Width / Height selects the resize balance. Rect Transform "
	         "uses anchors and a pivot in its parent rectangle, with Y "
	         "downward; UI Position and Size Delta use Canvas units. Stretch "
	         "fills the parent plus Size Delta. 3D Transform does not place "
	         "screen UI. A visible Text/Image needs Rect Transform and a "
	         "Canvas ancestor; incomplete authoring elements are omitted from "
	         "preview and reported before Play. Font Family defaults to the bundled "
	         "M PLUS 1p and also accepts Windows font names. Text and image colors include alpha. An Image "
	         "with no asset draws a solid rectangle; Preserve Aspect fits an "
	         "assigned image inside its rectangle. Canvas Sort Order controls "
	         "overlapping canvases; hierarchy order controls their children. "
	         "Settings, creation and removal support Undo/Redo and Prefabs. "
	         "Runtime Inspector is read-only. Lua provides get_text/set_text, "
	         "get_ui_position/set_ui_position(x,y), "
	         "get_ui_size/set_ui_size(widthDelta,heightDelta), "
	         "get_text_color/set_text_color(r,g,b,a) and "
	         "get_image_color/set_image_color(r,g,b,a). Values use existing UI "
	         "components and participate in frame rollback. Images are limited "
	         "to 4096 pixels per side, text to 4096 UTF-8 bytes, visible "
	         "elements to 256 and displayed textures to 128 MiB. Game output "
	         "and import retain Canvas settings, text and image dependencies."},
	        {"UI Buttons",
	         "Create > UI > Button makes a rectangle with a background, "
	         "centered label and Button. Inspector configures Interactable, "
	         "Button Navigation and normal/highlighted/pressed/disabled color "
	         "multipliers. Color feedback affects an Image on the same object, "
	         "or Text when there is no visible Image. Button needs Rect "
	         "Transform and a Canvas ancestor; an Image, label and Lua Script "
	         "are optional. In Play, press and release on the same button to "
	         "click. Leaving the target, losing focus, pausing or changing its "
	         "rectangle/parent cancels a press; returning while still holding "
	         "does not click. Child labels forward hits to the nearest Button. "
	         "A Text/Image with Raycast Target blocks UI behind it; clearing "
	         "that flag lets hits pass through. Buttons always use their own "
	         "rectangle for hits. Tab/Shift+Tab cycle buttons by default; arrows or "
	         "gamepad D-pad select by direction, and Enter/Space or the south "
	         "gamepad button activates the selection once per press by default. Use "
	         "Game Settings > Game UI Input to change bindings and input capture. Click "
	         "empty Game space or move the pointer to return to pointer "
	         "feedback. UI selection consumes movement/jump/navigation input; "
	         "UI pointer hits consume orbit input. Add Lua Script to the "
	         "button and return a click(self,state) function for its action. "
	         "Missing callbacks are inert. Use self:is_interactable() and "
	         "self:set_interactable(bool) or named object references for "
	         "custom actions. Click runs after update/Trigger callbacks inside "
	         "the same positive game step and shares their execution budget "
	         "and rollback. Pause does not activate buttons; Step accepts only "
	         "the current input. Stop/Restart/scene changes clear old UI "
	         "presses and selection. Button settings support Undo/Redo, "
	         "Prefabs, game output and import."},
	        {"Script Prefabs",
	         "In Inspector > Lua Script, enter a unique Prefab Name and Add "
	         "Prefab. Assign a .prefab.json using the combo or drag it from "
	         "Assets. None keeps an unassigned slot; Remove Prefab removes "
	         "the slot. Assignments support Undo/Redo, copying and Prefab "
	         "instances. Scene format v33 stores the names and paths; older "
	         "scenes start without Prefab slots. Game output and import "
	         "collect "
	         "nested Prefabs and their model, image, audio, Lua and scene "
	         "transition dependencies. Cycles are allowed and each asset is "
	         "processed once, within the shared 256 scene/Prefab limit. "
	         "In Lua callbacks use local object = "
	         "self:instantiate(\"enemy\",x,y,z). "
	         "The name must be assigned on that object. The returned root "
	         "has new IDs and remapped internal references; its scripts start "
	         "on the next positive step. Prefabs and their "
	         "Lua/model/image/audio "
	         "assets are prepared before Play. Pause does not spawn; Step runs "
	         "one step and Restart removes generated objects. Limits are 16 "
	         "calls per callback, 32 per step, 1024 generated objects per step "
	         "and 4096 scene objects. Any callback, physics or preparation "
	         "failure restores the entire simulation step."},

	        {"Animator",
	         "Assign a GLB/glTF model with animation to Mesh, then add "
	         "Animator. Choose a Default Clip, Speed, Loop and Play On Start "
	         "in Inspector. Empty Default Clip uses the model's first clip. "
	         "Settings support Undo/Redo, Scene/Prefab save, import and game "
	         "output. LINEAR and STEP curves are supported; export cubic "
	         "splines as sampled linear keys. Animator changes visual bones "
	         "and mesh nodes, while Character Controller, Rigidbody and "
	         "Collider keep using the object's Transform. It can coexist with "
	         "Transform Animation and Motion Path. Play copies the scene; "
	         "Pause freezes the clock, Step advances once, Restart resets it, "
	         "inactive branches freeze and hide their poses. Lua can call "
	         "play_clip(name, restart), play_clip(nil, false) to resume, "
	         "cross_fade_clip(name, seconds) to blend from the displayed pose "
	         "to a different clip over 0 to 10 seconds. A positive fade to "
	         "the current clip keeps its clock, while zero restarts instantly. "
	         "The fade uses game time and pauses with the Animator. "
	         "pause_animator(), stop_animator() (returns to bind pose), "
	         "seek_animator(seconds), set_animator_speed(speed), "
	         "set_animator_loop(loop), get_animator_time(), "
	         "get_animator_duration(), get_animator_speed(), "
	         "get_animator_clip() and is_animator_playing(). Runtime clip, "
	         "speed and loop changes do not edit the saved component. A failed "
	         "script or physics update restores the previous clip, time and "
	         "pose. Declared Prefab models are prepared before callbacks; a "
	         "newly instantiated object can play a clip immediately. Meshes "
	         "support eight bone influences per vertex and 256 bones per mesh. "
	         "Color, shadow, bounds and picking use the same evaluated pose. "
	         "Restart Play after reloading a model while playing."},
	        {"Particle Emitter",
	         "Add Component > Particle Emitter creates visual smoke, sparks "
	         "and bursts "
	         "without a Collider. Choose Soft Circle or an image from Assets; "
	         "texture "
	         "drag/drop is supported. Set a rate, starting burst, duration and "
	         "lifetime "
	         "range. Loop repeats the starting burst each cycle. Local "
	         "particles follow "
	         "their emitter; World particles stay at their birth positions. "
	         "Gravity uses "
	         "world axes in either space. The emission disk and cone use the "
	         "object's "
	         "local Y axis. Sizes and colors interpolate over each particle's "
	         "lifetime. "
	         "Alpha blending sorts from back to front; Additive accumulates "
	         "light. "
	         "Particles read 3D depth without writing it, are Unlit and do not "
	         "cast shadows. "
	         "Pause freezes particles, Step advances once, Restart resets "
	         "their seed; "
	         "inactive branches freeze and hide their particles. Lua can call "
	         "play_particles() (restart), play_particles(false) (resume "
	         "emission), "
	         "stop_particles() (existing particles keep aging), "
	         "clear_particles(), "
	         "emit_particles(count), get_particle_count() and "
	         "is_emitting_particles(). "
	         "Script or physics failures restore particles and their random "
	         "state. "
	         "Capacity is 4096 per emitter and 32768 per scene; overflow drops "
	         "new "
	         "particles. Images are limited to 4096 pixels and 128 MiB per "
	         "view. "
	         "Settings support Undo/Redo, Scene/Prefab save, import and game "
	         "output."},
	        {"Transform Animation",
	         "Add Component > Transform Animation animates local position "
	         "offsets, rotation "
	         "offsets and scale multipliers from the object's starting "
	         "Transform. Keys begin "
	         "at 0, use strictly increasing seconds and support Linear, Smooth "
	         "or Step "
	         "interpolation. Rotation follows the shortest quaternion path; "
	         "add intermediate "
	         "keys for full turns. Keep each scale axis nonzero and with the "
	         "same sign across "
	         "keys. Once stops on the last key; Loop wraps to the start; Ping "
	         "Pong reverses. "
	         "Play On Start applies the first key at startup. Editing keys, "
	         "settings, Add, "
	         "Reset and Remove support Undo/Redo. Play leaves the authored "
	         "Transform alone; "
	         "Pause freezes time, Step advances once and Restart resets it. "
	         "Inactive branches "
	         "freeze their clocks. The same object cannot also use Motion "
	         "Path, Character "
	         "Controller or Follow Camera; a Rigid Body must be Kinematic. Use "
	         "Motion Path "
	         "for passenger-carrying platforms. Lua callbacks can call "
	         "play_animation() "
	         "(restart), play_animation(false) (resume), pause_animation(), "
	         "stop_animation() "
	         "(restore the starting pose), seek_animation(seconds), "
	         "is_animation_playing() "
	         "and get_animation_time(). Seeking a Loop to its duration wraps "
	         "to 0. Changes "
	         "are transactional with physics and scripts. Limits: 64 keys, "
	         "3600 seconds, "
	         "speed 0-16 (exclusive of 0), 128 Lua operations per callback and "
	         "1024 per step. "
	         "Scene/Prefab save, cloning and game output retain the keys."},
	        {"Directional Shadows", "Game Settings > Directional Shadows "
	                                "enables a camera-centered shadow map "
	                                "for the global directional light. "
	                                "Resolution is 512, 1024 or 2048. Extent "
	                                "is half the covered width in world units; "
	                                "increase it for a larger stage "
	                                "or decrease it for finer detail. Strength "
	                                "affects direct sunlight only, "
	                                "leaving ambient, point/spot lights and "
	                                "Unlit materials unchanged. Depth "
	                                "and Normal Bias reduce self-shadow "
	                                "artifacts; excessive values detach "
	                                "shadows. The edge fades into unshadowed "
	                                "lighting. Visible meshes cast "
	                                "and receive shadows; hidden/inactive "
	                                "meshes are excluded. Scene and "
	                                "Game keep separate maps. Apply updates "
	                                "Scene and the next Play; settings "
	                                "save and game output retain the "
	                                "configuration. Old configurations keep "
	                                "shadows disabled."},
	        {"Named Input Actions",
	         "Game Settings > Named Input Actions assigns up to 32 unique "
	         "names to "
	         "keyboard, mouse, gamepad buttons, triggers and stick directions. "
	         "An axis counts as pressed at 60% and releases at 40%, after the "
	         "stick dead zone is applied. None leaves a binding "
	         "unassigned; "
	         "an entirely unassigned action stays neutral. Apply affects the "
	         "next Play; "
	         "Save Game Configuration, Editor preferences and game output "
	         "retain the bindings. "
	         "In update(self,state,dt,input), read input.actions.fire.down, "
	         ".pressed, .released or .value. Value is the strongest assigned "
	         "input from 0 to 1, even below the button threshold. "
	         "Keys and buttons use 0 or 1. "
	         "(use input.actions[\"name\"] for other names). Check the entry "
	         "exists if optional. "
	         "Buttons from different devices combine into one operation: "
	         "pressed occurs once "
	         "when any becomes held, released when all are released. Game must "
	         "have focus; "
	         "mouse input also requires hovering its image. Text editing, "
	         "modal windows and "
	         "game UI interaction suppress actions. Start, focus return, mouse "
	         "re-entry and "
	         "device reconnection require releasing held buttons first. Pause "
	         "consumes input "
	         "edges without running Lua; Step uses that frame only. Restart "
	         "resets input. "
	         "Enable Gamepad also controls these bindings. Movement keys "
	         "remain independent."},
	        {"Script Messages",
	         "Use target:send_message(\"damage\", {amount=1}) on an object "
	         "reference. It returns false if the target has no active Lua "
	         "Script; missing message handlers are inert. Return "
	         "message(self,state,name,data,sender) to receive it after the "
	         "normal callbacks of the next positive step. Data is copied at "
	         "send time; omitted data means {}. The sender can already be "
	         "deleted: use sender:is_valid() before accessing its properties. "
	         "Messages sent from a handler wait until another step, so "
	         "delivery never recurses. Deleted/inactive receivers are skipped. "
	         "Pause holds messages; Step delivers once. Restart, Stop and "
	         "Scene changes clear them. Any callback failure restores both "
	         "incoming and outgoing messages and the whole simulation step. "
	         "Names use 1-64 UTF-8 bytes, data is a 4 KiB JSON object using "
	         "state-compatible values. Limits: 32 sends per callback, 128 per "
	         "step, 128 pending messages and 64 KiB for names/data. Play State "
	         "shows the queued count."},
	        {"Shared Script Data",
	         "Use game.score, game.inventory or other fields from callbacks "
	         "to share data across objects and Scene transitions. For example, "
	         "game.score = (game.score or 0) + 1. Unlike each object's state, "
	         "game survives load_scene and Trigger Scene changes. Callbacks "
	         "see changes made by earlier callbacks; "
	         "any failure restores the whole step, including shared data. "
	         "Play State > Shared Script Data displays the current JSON. "
	         "Pause holds it; Step advances once. Start, Restart and Stop "
	         "reset it. This data lasts for one Play session. Each shared root "
	         "uses the same 64 KiB, 4096-value and depth-16 limits as state. "
	         "Use finite numbers, strings, booleans and tables. Functions, "
	         "userdata, sparse/mixed arrays and cycles are invalid. Modify "
	         "game's fields instead of replacing its root table, and do not "
	         "change shared data at module scope."},
	        {"Script Scenes",
	         "In Inspector > Lua Script, add a unique Scene Name and assign "
	         "a .scene.json from the combo or Assets drag/drop. Call "
	         "self:load_scene(\"level\") from a callback, including a Button "
	         "click. The last successful request wins and loads on the next "
	         "positive step; the current callback finishes first. Limits are "
	         "8 requests per callback and 64 per step. Errors discard the "
	         "whole simulation step; a failed destination load preserves the "
	         "current scene. Pause holds a request until Step or resume. "
	         "Restart returns to the original authored Play scene. Assignments "
	         "support Undo/Redo, Prefabs, output and import, including cyclic "
	         "Scene/Prefab dependencies within the shared 256-asset limit. "
	         "Scene format v34 stores the names; older scenes start empty."},
	        {"Audio Scripts",
	         "On an object with Audio Source, call play_audio() to restart "
	         "its sound from the beginning, or stop_audio() to stop it. Both "
	         "return false for an inactive or unassigned source. Use "
	         "self:reference(\"sound\") to control another assigned source. "
	         "get_audio_volume() reads its volume; set_audio_volume(value) "
	         "accepts 0 to 4 and preserves playback position and manual stop. "
	         "Calls are limited to 128 per callback and 1024 per step. "
	         "Commands "
	         "only reach audio after the full simulation succeeds. The last "
	         "play/stop command for each source wins. Normal steps run Trigger "
	         "Actions before Lua; at Play start, Lua start runs before initial "
	         "Trigger Actions. Deleting a source discards its requests. Pause "
	         "keeps audio paused; Step can queue a paused sound once and "
	         "Restart restores the authored volume and startup behavior."},
	        {"Destroying Objects",
	         "Inside a Lua callback call object:destroy(). It queues the "
	         "root and its descendants until all callbacks in that step "
	         "finish. "
	         "Repeated requests are harmless; the return value reports a new "
	         "request. Limits are 64 requests per callback and 256 per step. "
	         "All requests and Lua/physics changes roll back on any failure. "
	         "Remaining named references become nil. Follow Cameras whose "
	         "target is removed stop following at their current pose, and "
	         "Trigger Actions using removed targets or teleport destinations "
	         "are removed from the Play copy only. Surviving objects receive "
	         "trigger_exit/collision_exit on the next step. A deleted other "
	         "still supports get_id() and is_valid(); accessing its other "
	         "properties is an error. Pause defers deletion until Step or "
	         "resume; Restart restores authored objects. Destroyed IDs are "
	         "not reused within that run."},
	        {"Collision Scripts",
	         "Return collision_enter(self,state,other), collision_stay or "
	         "collision_exit for solid Collider contacts. These are separate "
	         "from Trigger and honor both Colliders' layer masks and active "
	         "ancestors. Rigidbody and static worlds use actual shapes; scenes "
	         "without a Rigid Body do not need a physics component to notify. "
	         "Collider-only pairs can notify too. Character Controller child "
	         "Colliders are excluded. Contacts within 0.0002 world units count "
	         "as touching. Sleeping bodies keep Stay. Native physical impacts "
	         "that separate within one game step send Enter then Exit. "
	         "Callbacks run in pair-ID order on both active scripts, after "
	         "update/Trigger and before click. Initial contacts are evaluated "
	         "on the first positive step. Transform/activity changes made by "
	         "callbacks affect contacts on the next step, without recursion. "
	         "Pause holds notifications; Step advances once; Restart clears "
	         "contacts. Scene, scripts, physics and notifications roll back "
	         "together on failure. All callbacks share the existing game-step "
	         "budgets. Static paths are not swept for collision "
	         "notifications."},
	        {"Raycast Scripts",
	         "scene.raycast(ox,oy,oz, dx,dy,dz, distance [,layerMask "
	         "[,includeTriggers [,ignoredObject]]]) finds the nearest active "
	         "Collider, including rotated/scaled children. Direction is "
	         "normalized; distance is in world units from 0 to 1000000. The "
	         "default mask includes all layers, Trigger is excluded and the "
	         "script owner is ignored. The mask selects Collider layers, "
	         "independently of their collision masks. Returns nil on a miss, "
	         "or a table with object, distance, x/y/z and normal_x/y/z. A ray "
	         "starting inside returns distance 0 and the opposite ray "
	         "direction "
	         "as normal. Queries see earlier script transform/activity changes "
	         "and share the physics API call budget. Use hit.object references "
	         "to interact with the target or apply impulses. Only assigned "
	         "Colliders participate; model triangles are not queried."},
	        {"Lua > Game Camera",
	         "scene.get_camera() returns the active Primary Camera or nil. "
	         "scene.set_camera(object) selects an active object with Camera; "
	         "pass nil to use the fixed fallback view. Call the setter inside "
	         "a callback. Both fixed and Follow Cameras are supported. "
	         "Restart restores the authored Primary selection."},
	        {"Lua > Character Speeds",
	         "get_character_speeds() returns move speed and jump speed. "
	         "set_character_speeds(move,jump) changes both for later movement "
	         "and jumps. Values are in world units per second, greater than "
	         "0 and at most 1000. An ongoing jump keeps its vertical "
	         "velocity."},
	        {"Lua > Set Checkpoint",
	         "character:set_checkpoint(x,y,z) updates the fall recovery "
	         "position of an active Character Controller with Respawn. Call "
	         "it in a callback. The destination must be on the safe side of the Fall Boundary "
	         "and clear of solids after the callback changes are applied. "
	         "The current position and velocity are preserved. Restart "
	         "starts from the authored Respawn settings."},
	        {"Lua > Character State",
	         "In update, input.character describes the active Character "
	         "Controller after movement and Trigger Actions. It provides "
	         "object, grounded, ground (Collider object or nil), "
	         "verticalVelocity and respawnCount. The snapshot is shared "
	         "for the update and is nil when there is no active character."},
	        {"Lua > Viewport Input",
	         "In update, input.viewport gives width and height in pixels. "
	         "input.pointer gives normalized top-left x/y and left-button "
	         "down/pressed when the pointer is available. It is nil outside "
	         "the game image, while gameplay input is blocked, or when game "
	         "UI consumes the pointer. Combine it with camera:viewport_ray "
	         "for picking."},
	        {"Lua > Viewport Ray",
	         "camera:viewport_ray(u,v,width,height) returns origin x/y/z, "
	         "unit direction x/y/z, and distance to the far clip plane. "
	         "Use coordinates from 0 to 1 with a top-left origin. The ray "
	         "starts at the near clip plane. Pass these values to "
	         "scene.raycast for picking or aiming."},
	        {"Lua > World To Viewport",
	         "camera:world_to_viewport(x,y,z,width,height) returns normalized "
	         "u/v from the top left, forward distance, and a visible flag. "
	         "Pass the viewport's pixel dimensions for the correct aspect. "
	         "Off-screen points still return coordinates; points at or behind "
	         "the camera return nil. Visibility tests the view bounds and clip "
	         "distances, without testing occlusion by scene objects."},
	        {"Lua > Camera Lens",
	         "On a Camera object, get_camera_lens() returns vertical FOV "
	         "in degrees and the near and far clip distances. Use "
	         "set_camera_fov(degrees) for zoom or set_camera_lens(degrees, "
	         "near, far) to update all three values. Both setters apply "
	         "during callbacks and preserve the camera pose."},
	        {"Lua > Mesh Appearance",
	         "For an object with Mesh Renderer, set_mesh_visible(boolean) "
	         "changes its mesh visibility while gameplay components continue "
	         "running. get_mesh_visible() reads the renderer setting. "
	         "set_mesh_tint(r,g,b) multiplies the mesh color with values from "
	         "0 to 1. get_mesh_tint() returns RGB (white by default). Make "
	         "changes in callbacks. Restart restores the authored settings."},
	        {"Lua > Set Parent",
	         "object:set_parent(parent [,keepWorld]) changes its parent during "
	         "a callback. Pass nil to move it to the scene root. By default "
	         "the world transform is preserved. Pass false to retain the "
	         "local transform, for example when attaching a held item. "
	         "Cycles and transforms that cannot be represented are rejected."},
	        {"Lua > Child Objects",
	         "object:find_child(name) returns the first direct child with "
	         "the exact name, or nil. object:get_children() returns up to "
	         "256 direct children in ID order, including inactive children. "
	         "Its second result reports whether more children exist. Use "
	         "these on a Prefab instance to find its own parts."},
	        {"Lua > Coordinate Conversion",
	         "Use object:local_to_world(x,y,z) for a muzzle or attachment "
	         "point and object:world_to_local(x,y,z) for the reverse. "
	         "local_vector_to_world and world_vector_to_local transform "
	         "offsets without translation or normalization. The full object "
	         "and ancestor transforms apply. All four calls return x, y, z."},
	        {"Lua > Look At",
	         "object:look_at(x,y,z) points its local +Z axis toward a world "
	         "position, preserving its position and scale. A coincident "
	         "target leaves rotation as it is. Pass the object's own world "
	         "Y coordinate to aim horizontally. Use inside a callback."},
	        {"Inspector > Tag",
	         "Assign one case-sensitive tag per object (up to 64 UTF-8 bytes). "
	         "Clear the field to remove it. Filter the Hierarchy with "
	         "tag:Enemy. "
	         "Tags are saved with scenes and "
	         "Prefabs. Lua can read object:get_tag() or call "
	         "scene.find_all_by_tag(tag [,includeInactive]). The returned list "
	         "uses object ID order and holds up to 256 matches. The second "
	         "result reports whether more objects matched."},
	        {"Sphere Cast Scripts",
	         "scene.cast_sphere(ox,oy,oz, dx,dy,dz, distance, radius "
	         "[,layerMask [,includeTriggers [,ignoredObject]]]) sweeps a "
	         "sphere "
	         "along a direction and returns the first Collider hit, or nil. "
	         "Filters and result fields match scene.raycast. The x/y/z fields "
	         "are the sphere center at impact. An initial overlap returns "
	         "distance 0 and the opposite direction as normal. Use it for "
	         "projectiles or movement probes that need a radius."},
	        {"Sphere Overlap Scripts",
	         "scene.overlap_sphere(x,y,z,radius [,layerMask "
	         "[,includeTriggers [,ignoredObject]]]) returns a list of active "
	         "Collider objects that overlap a world-space sphere. Touching its "
	         "surface alone does not count. The optional filters have the same "
	         "defaults as scene.raycast. Results use object ID order. The "
	         "second "
	         "return value is true if more than 256 objects matched. Use the "
	         "list to apply area effects or detect nearby targets. Queries see "
	         "earlier script edits and share the physics API call budget."},
	        {"View menu", "Show or hide the grid and selection bounds, or "
	                      "reset the dock layout."},
	    });

	ImGui::End();
}
