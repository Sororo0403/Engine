#include "panels/HierarchyPanel.h"

#include "graphics/ui/ImGuiSystem.h"
#include "scene/Scene.h"
#include "selection/EditorSelection.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <utility>
#include <vector>

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
	}
	if (!scene.Contains(rangeAnchorId_)) {
		rangeAnchorId_ = 0;
	}
	lastActiveId_ = activeId;

	if (!ui.BeginWindow("Hierarchy")) {
		ui.EndWindow();
		return result;
	}

	ui.InputText("Search", filter_);

	ui.SameLine();
	ui.BeginDisabled(filter_.empty());

	if (ui.Button("Clear")) {
		// 検索だけを解除し、範囲選択の基準は保持する
		filter_.clear();
	}

	ui.EndDisabled();

	ui.Text(std::format("Selected: {}", selection.GetCount()));

	const SceneObject *active = scene.FindObject(selection.GetActiveId());

	if (active) {
		const std::string_view name = active->name.empty()
		                                  ? std::string_view{"Object"}
		                                  : std::string_view{active->name};

		ui.Text(std::format("Active: {}", name));
	}

	std::size_t hiddenCount = 0;

	for (const SceneObjectId id : selection.GetIds()) {
		const SceneObject *object = scene.FindObject(id);

		if (!object) {
			continue;
		}

		const std::string_view name = object->name.empty()
		                                  ? std::string_view{"Object"}
		                                  : std::string_view{object->name};

		if (!MatchesFilter(name)) {
			++hiddenCount;
		}
	}

	if (hiddenCount != 0) {
		ui.Text(std::format("Hidden selections: {}", hiddenCount));
	}

	if (selection.GetCount() > 1) {
		ui.Text("Inspector: active object only.");
	}

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

			if (MatchesFilter(name)) {
				visibleIds.push_back(id);
			}
		}

		// 描画と範囲選択で同じ表示一覧を使用し、検索で隠れた項目を除く
		for (const SceneObjectId id : visibleIds) {
			const SceneObject &object = objects.at(id);
			const std::string_view name = object.name.empty()
			                                  ? std::string_view{"Object"}
			                                  : std::string_view{object.name};

			// アクティブ物体に印を付けても、項目のIDは変えない
			const auto label = std::format(
			    "{}{}###SceneObject{}",
			    selection.GetActiveId() == id ? "> " : "  ", name, id);

			if (ui.Selectable(label.c_str(), selection.Contains(id))) {
				const auto &io = ImGui::GetIO();
				const bool changed = SelectObject(
				    id, visibleIds, io.KeyCtrl, io.KeyShift, selection);

				if (changed) {
					result.selectionChanged = true;
				}
			}
			if (ImGui::BeginPopupContextItem()) {
				// 選択済みの項目ではグループを保ち、未選択の項目だけ選び直す
				if (!selection.Contains(id)) {
					result.selectionChanged |= selection.SelectOnly(id);
					rangeAnchorId_ = id;
				}
				if (ui.MenuItem("Copy", "Ctrl+C", true)) {
					result.action = Action::COPY_OBJECTS;
				}
				if (ui.MenuItem("Cut", "Ctrl+X", true)) {
					result.action = Action::CUT_OBJECTS;
				}
				if (ui.MenuItem("Duplicate", "Ctrl+D", true)) {
					result.action = Action::DUPLICATE_OBJECTS;
				}
				ui.Separator();
				if (ui.MenuItem("Delete", "Delete", true)) {
					result.action = Action::DELETE_OBJECTS;
				}
				if (ui.MenuItem("Reset Transform", nullptr, true)) {
					result.action = Action::RESET_TRANSFORMS;
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
	rangeAnchorId_ = 0;
	lastActiveId_ = 0;
}

bool HierarchyPanel::MatchesFilter(std::string_view name) const {
	return filter_.empty() || name.find(filter_) != std::string_view::npos;
}

bool HierarchyPanel::SelectAllVisible(const Scene &scene,
                                     EditorSelection &selection) {
	std::vector<SceneObjectId> ids;
	for (const auto &[id, object] : scene.GetObjects()) {
		const std::string_view name = object.name.empty()
		                                  ? std::string_view{"Object"}
		                                  : std::string_view{object.name};
		if (MatchesFilter(name)) {
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
