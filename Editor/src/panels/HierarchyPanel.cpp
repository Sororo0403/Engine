#include "panels/HierarchyPanel.h"

#include "graphics/ui/ImGuiSystem.h"
#include "scene/Scene.h"
#include "selection/EditorSelection.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <utility>
#include <vector>

void HierarchyPanel::RequestReveal(SceneObjectId objectId) {
	scrollToId_ = objectId;
	revealRequested_ = objectId != 0;
}

HierarchyPanel::Result HierarchyPanel::Draw(const ImGuiSystem &ui,
                                            const Scene &scene,
                                            EditorSelection &selection) {
	Result result;

	// 検索で隠れた物体は残し、削除済みの物体だけ取り除く
	result.selectionChanged = selection.RemoveMissing(scene);

	// Sceneビューや複製など、Hierarchy以外で変わった選択も基準へ反映する
	const SceneObjectId activeId = selection.GetActiveId();
	if (activeId != lastActiveId_) {
		rangeAnchorId_ = activeId;
		if (!revealRequested_) {
			scrollToId_ = activeId;
		}
	}
	if (!scene.Contains(rangeAnchorId_)) {
		rangeAnchorId_ = 0;
	}
	lastActiveId_ = activeId;
	if (!scene.Contains(scrollToId_)) {
		scrollToId_ = 0;
		revealRequested_ = false;
	}
	if (revealRequested_) {
		filter_.clear();
		hiddenOnly_ = false;
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
	}

	if (!ui.BeginWindow("Hierarchy")) {
		ui.EndWindow();
		return result;
	}
	revealRequested_ = false;

	ui.InputText("Search", filter_);

	ui.SameLine();
	ui.BeginDisabled(filter_.empty());

	if (ui.Button("Clear")) {
		// 検索だけを解除し、範囲選択の基準は保持する
		filter_.clear();
	}

	ui.EndDisabled();

	ImGui::Checkbox("Hidden only", &hiddenOnly_);
	const SceneObject *active = scene.FindObject(selection.GetActiveId());

	if (active) {
		const std::string_view name = active->name.empty()
		                                  ? std::string_view{"Object"}
		                                  : std::string_view{active->name};

		ui.Text(std::format("Active: {}", name));
	} else {
		ui.Text("Active: None");
	}

	std::size_t filteredCount = 0;

	for (const SceneObjectId id : selection.GetIds()) {
		const SceneObject *object = scene.FindObject(id);

		if (!object) {
			continue;
		}

		const std::string_view name = object->name.empty()
		                                  ? std::string_view{"Object"}
		                                  : std::string_view{object->name};

		if (!MatchesFilter(name, object->visible)) {
			++filteredCount;
		}
	}

	// 選択状態に関係なく行数を保ち、クリック中に一覧の位置を動かさない
	ui.Text(std::format("Selected: {} | filtered: {}", selection.GetCount(), filteredCount));
	ImGui::SetItemTooltip("The Inspector edits the active object. Up/Down and Home/End navigate the filtered list; Shift selects a range. Ctrl+click toggles objects.");

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
		std::vector<SceneObjectId> visibleIds;
		visibleIds.reserve(objects.size());

		for (const auto &[id, object] : objects) {
			const std::string_view name = object.name.empty()
			                                  ? std::string_view{"Object"}
			                                  : std::string_view{object.name};

			if (MatchesFilter(name, object.visible)) {
				visibleIds.push_back(id);
			}
		}

		// 描画と範囲選択で同じ表示一覧を使用し、検索で隠れた項目を除く
		SceneObjectId keyboardFocusId = 0;
		result.selectionChanged |= NavigateSelection(visibleIds, selection, keyboardFocusId);
		for (const SceneObjectId id : visibleIds) {
			const SceneObject &object = objects.at(id);
			const std::string_view name = object.name.empty()
			                                  ? std::string_view{"Object"}
			                                  : std::string_view{object.name};

			// アクティブ物体に印を付けても、項目のIDは変えない
			const auto label = std::format(
			    "{}{}{}###SceneObject{}",
			    selection.GetActiveId() == id ? "> " : "  ",
			    object.visible ? "" : "[Hidden] ", name, id);

			const auto &io = ImGui::GetIO();
			if (ui.Selectable(label.c_str(), selection.Contains(id))) {
				const bool changed = SelectObject(
				    id, visibleIds, io.KeyCtrl, io.KeyShift, selection);
				scrollToId_ = 0;

				if (changed) {
					result.selectionChanged = true;
				}
			}
			if (keyboardFocusId == id) {
				ImGui::SetKeyboardFocusHere(-1);
			}
			// マウス操作中の一覧を動かさず、表示対象の位置へスクロールする
			if (scrollToId_ == id && !ImGui::IsAnyItemActive() &&
			    !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
			    !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
			    !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
				ImGui::SetScrollHereY(0.5f);
				scrollToId_ = 0;
			}
			if (object.visible && !object.modelPath.empty() && ImGui::IsItemHovered() &&
			    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
			    !io.KeyCtrl && !io.KeyShift && !io.KeyAlt) {
				result.selectionChanged |= SelectObject(id, visibleIds, false, false, selection);
				result.action = Action::FRAME_SELECTED;
				scrollToId_ = 0;
			}
			if (ImGui::BeginPopupContextItem()) {
				// 選択済みの項目ではグループを保ち、未選択の項目だけ選び直す
				if (!selection.Contains(id)) {
					result.selectionChanged |= selection.SelectOnly(id);
					rangeAnchorId_ = id;
					scrollToId_ = 0;
				}
				if (ui.MenuItem("Copy", "Ctrl+C", true)) {
					result.action = Action::COPY_OBJECTS;
				}
				if (ui.MenuItem("Frame Selected", "F", true)) {
					result.action = Action::FRAME_SELECTED;
				}
				if (ui.MenuItem("Rename", "F2", selection.GetCount() != 0)) {
					result.action = Action::RENAME_OBJECT;
				}
				if (ui.MenuItem("Cut", "Ctrl+X", true)) {
					result.action = Action::CUT_OBJECTS;
				}
				if (ui.MenuItem("Duplicate", "Ctrl+D", true)) {
					result.action = Action::DUPLICATE_OBJECTS;
				}
				bool canHide = false;
				bool canShow = false;
				bool canGround = false;
				for (const auto selectedId : selection.GetIds()) {
					if (const auto *selected = scene.FindObject(selectedId)) {
						canHide |= selected->visible;
						canShow |= !selected->visible;
						canGround |= selected->visible && !selected->modelPath.empty();
					}
				}
				if (ui.MenuItem("Hide Selected", nullptr, canHide)) {
					result.action = Action::HIDE_OBJECTS;
				}
				if (ui.MenuItem("Show Selected", nullptr, canShow)) {
					result.action = Action::SHOW_OBJECTS;
				}
				ui.Separator();
				if (ui.MenuItem("Delete", "Delete", true)) {
					result.action = Action::DELETE_OBJECTS;
				}
				if (ui.MenuItem("Reset Transform", nullptr, true)) {
					result.action = Action::RESET_TRANSFORMS;
				}
				if (ui.MenuItem("Drop to Ground", "Shift+G", canGround)) {
					result.action = Action::DROP_TO_GROUND;
				}
				ImGui::EndPopup();
			}
		}

		if (visibleIds.empty()) {
			ui.Text(objects.empty() ? "No objects." : "No matching objects.");
		}
		if (ImGui::BeginPopupContextWindow(
		        "HierarchyBackground", ImGuiPopupFlags_MouseButtonRight |
		                                   ImGuiPopupFlags_NoOpenOverItems)) {
			if (ui.MenuItem("Create Empty", nullptr, true)) {
				result.action = Action::CREATE_EMPTY;
			}
			ImGui::EndPopup();
		}
	}

	ui.EndChild();

	result.windowFocused =
	    ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows |
	                           ImGuiFocusedFlags_NoPopupHierarchy);

	ui.EndWindow();
	lastActiveId_ = selection.GetActiveId();
	return result;
}

void HierarchyPanel::Clear() {
	filter_.clear();
	hiddenOnly_ = false;
	rangeAnchorId_ = 0;
	lastActiveId_ = 0;
	scrollToId_ = 0;
	revealRequested_ = false;
}

bool HierarchyPanel::MatchesFilter(std::string_view name, bool visible) const {
	return (!hiddenOnly_ || !visible) &&
	       (filter_.empty() || name.find(filter_) != std::string_view::npos);
}

bool HierarchyPanel::SelectAllVisible(const Scene &scene,
                                     EditorSelection &selection) {
	std::vector<SceneObjectId> ids;
	for (const auto &[id, object] : scene.GetObjects()) {
		const std::string_view name = object.name.empty()
		                                  ? std::string_view{"Object"}
		                                  : std::string_view{object.name};
		if (MatchesFilter(name, object.visible)) {
			ids.push_back(id);
		}
	}
	const auto current = selection.GetIds();
	if (std::equal(current.begin(), current.end(), ids.begin(), ids.end())) {
		return false;
	}
	rangeAnchorId_ = ids.empty() ? 0 : ids.front();
	const bool changed = selection.Set(std::move(ids));
	lastActiveId_ = selection.GetActiveId();
	return changed;
}

bool HierarchyPanel::NavigateSelection(
    std::span<const SceneObjectId> visibleIds, EditorSelection &selection,
    SceneObjectId &focusId) {
	focusId = 0;
	const auto &io = ImGui::GetIO();
	if (io.AppFocusLost || io.WantTextInput || io.KeyAlt ||
	    ImGui::IsAnyItemActive() || (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0 ||
	    !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows |
	                            ImGuiFocusedFlags_NoPopupHierarchy) ||
	    ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
		return false;
	}
	for (const auto button : {ImGuiMouseButton_Left, ImGuiMouseButton_Right, ImGuiMouseButton_Middle}) {
		if (ImGui::IsMouseDown(button) || ImGui::IsMouseReleased(button)) {
			return false;
		}
	}

	ImGuiKey pressed = ImGuiKey_None;
	bool control = false;
	bool shift = false;
	const auto flags = ImGuiInputFlags_Repeat | ImGuiInputFlags_RouteFromRootWindow;
	for (const auto key : {ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Home, ImGuiKey_End}) {
		// 修飾キーを同時に押した最初のフレームにも届くよう、全ての組み合わせを登録する
		const bool plain = ImGui::Shortcut(key, flags);
		const bool range = ImGui::Shortcut(ImGuiMod_Shift | key, flags);
		const bool addRange = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | key, flags);
		if (pressed == ImGuiKey_None && (plain || range || addRange)) {
			pressed = key;
			control = addRange;
			shift = range || addRange;
		}
	}
	if (pressed == ImGuiKey_None || visibleIds.empty()) {
		return false;
	}
	const auto active = std::find(visibleIds.begin(), visibleIds.end(), selection.GetActiveId());
	std::size_t index = 0;
	if (pressed == ImGuiKey_End || (active == visibleIds.end() && pressed == ImGuiKey_UpArrow)) {
		index = visibleIds.size() - 1;
	} else if (pressed != ImGuiKey_Home && active != visibleIds.end()) {
		index = static_cast<std::size_t>(active - visibleIds.begin());
		if (pressed == ImGuiKey_UpArrow && index > 0) {
			--index;
		} else if (pressed == ImGuiKey_DownArrow && index + 1 < visibleIds.size()) {
			++index;
		}
	}
	focusId = visibleIds[index];
	scrollToId_ = focusId;
	return SelectObject(focusId, visibleIds, control, shift, selection);
}

bool HierarchyPanel::SelectObject(
    SceneObjectId objectId, std::span<const SceneObjectId> visibleIds,
    bool control, bool shift, EditorSelection &selection) {
	const auto clicked = std::find(visibleIds.begin(), visibleIds.end(), objectId);
	if (clicked == visibleIds.end()) {
		return false;
	}
	if (!shift) {
		rangeAnchorId_ = objectId;
		return control ? selection.Toggle(objectId)
		               : selection.SelectOnly(objectId);
	}

	auto anchor =
	    std::find(visibleIds.begin(), visibleIds.end(), rangeAnchorId_);
	if (anchor == visibleIds.end()) {
		// 基準が非表示、未指定、または削除済みならクリック先から開始する
		anchor = clicked;
		rangeAnchorId_ = objectId;
	}
	const auto [first, last] = std::minmax(anchor, clicked);

	std::vector<SceneObjectId> ids;
	if (control) {
		const auto current = selection.GetIds();
		ids.assign(current.begin(), current.end());
	}

	// クリック先は末尾に置き、逆方向の範囲選択でもアクティブにする
	std::erase(ids, objectId);
	for (auto it = first; it != last + 1; ++it) {
		if (*it != objectId &&
		    std::find(ids.begin(), ids.end(), *it) == ids.end()) {
			ids.push_back(*it);
		}
	}
	ids.push_back(objectId);

	const auto current = selection.GetIds();
	if (std::equal(current.begin(), current.end(), ids.begin(), ids.end())) {
		return false;
	}
	return selection.Set(std::move(ids));
}
