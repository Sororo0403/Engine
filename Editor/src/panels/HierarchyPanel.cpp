#include "panels/HierarchyPanel.h"
#include "scene/SceneActivity.h"

#include "gameplay/FollowCameraUsage.h"
#include "graphics/ui/ImGuiSystem.h"
#include "physics/ColliderPlacement.h"
#include "scene/Scene.h"
#include "scene/SceneHierarchy.h"
#include "selection/EditorSelection.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
/// <summary>
/// ASCII英字の大文字・小文字を区別せず表示名を比較する。ほかの文字はUTF-8の順序を使う
/// </summary>
/// <param name="first">比較する表示名</param>
/// <param name="second">比較対象の表示名</param>
/// <returns>負:前に並ぶ 0:同じ名前 正:後に並ぶ</returns>
int CompareNames(std::string_view first, std::string_view second) {
	const auto lower = [](unsigned char value) {
		return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
	};

	for (std::size_t i = 0; i < (std::min)(first.size(), second.size()); ++i) {
		const auto a = lower(static_cast<unsigned char>(first[i]));
		const auto b = lower(static_cast<unsigned char>(second[i]));

		if (a != b) {
			return a < b ? -1 : 1;
		}
	}

	return first.size() == second.size()  ? 0
	       : first.size() < second.size() ? -1
	                                      : 1;
}
} // namespace

void HierarchyPanel::RequestReveal(SceneObjectId objectId) {
	scrollToId_ = objectId;
	revealRequested_ = objectId != 0;
}

HierarchyPanel::Result
HierarchyPanel::Draw(const ImGuiSystem &ui, const Scene &scene,
                     EditorSelection &selection, bool editable,
                     bool canPasteCollider, bool canPasteCameraLens,
                     bool canPasteRespawn) {
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

	const bool focusWindow = revealRequested_;

	if (focusWindow) {
		filter_.Clear();
		pickingDisabledOnly_ = false;
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
	}

	const bool windowVisible = ui.BeginWindow("Hierarchy");

	if (focusWindow) {
		ui.FocusWindow("Hierarchy");
	}

	if (!windowVisible) {
		ui.EndWindow();
		return result;
	}

	revealRequested_ = false;

	ui.InputText("Search", filter_.text);
	ImGui::SetItemTooltip(
	    "Search by name, or type id:42 for an exact object ID.");

	ui.SameLine();
	ui.BeginDisabled(filter_.text.empty());

	if (ui.Button("Clear")) {
		// 検索だけを解除し、範囲選択の基準は保持する
		filter_.text.clear();
	}

	ui.EndDisabled();

	ImGui::Checkbox("Hidden only", &filter_.hiddenOnly);
	ImGui::Checkbox("Scene picking disabled only", &pickingDisabledOnly_);
	int component = static_cast<int>(filter_.component);

	if (ImGui::Combo(
	        "Component", &component,
	        "All objects\0Box Collider\0Models without Collider\0Character "
	        "Controller\0Camera\0Follow Camera\0Respawn\0Color "
	        "Tint\0Mesh\0Mesh Renderer\0Primary Camera\0Incomplete Mesh / "
	        "Renderer\0Characters without Collider\0")) {
		filter_.component = static_cast<HierarchyFilter::Component>(component);
	}

	int sort = static_cast<int>(sortMode_);

	if (ImGui::Combo("Sort", &sort, "Creation order\0Name A-Z\0Name Z-A\0")) {
		sortMode_ = static_cast<SortMode>(sort);
		// 並び順を変えた後の範囲選択は、現在のアクティブ物体を基準にする
		rangeAnchorId_ = selection.GetActiveId();
		scrollToId_ = selection.GetActiveId();
	}

	ImGui::SetItemTooltip("Names ignore ASCII letter case; ties keep creation "
	                      "order. Selection is kept.");
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

		if (!MatchesFilters(id, *object)) {
			++filteredCount;
		}
	}

	// 選択状態に関係なく行数を保ち、クリック中に一覧の位置を動かさない
	ui.Text(std::format("Selected: {} | filtered: {}", selection.GetCount(),
	                    filteredCount));
	ImGui::SetItemTooltip("Up/Down and Home/End navigate the filtered list; "
	                      "Shift selects a range. Ctrl+click toggles objects.");

	ui.BeginDisabled(!editable);

	if (ui.Button("Create Empty")) {
		result.action = Action::CREATE_EMPTY;
	}

	ui.SameLine();

	if (ui.Button("Create Box")) {
		result.action = Action::CREATE_BOX;
	}

	ui.BeginDisabled(selection.GetCount() == 0);

	if (ui.Button("Delete Selected")) {
		result.action = Action::DELETE_OBJECTS;
	}

	ui.EndDisabled();

	ui.BeginDisabled(scenePickingDisabled_.empty() && !pickingDisabledOnly_);

	if (ui.Button("Enable All Scene Picking") && editable) {
		scenePickingDisabled_.clear();
		pickingDisabledOnly_ = false;
	}

	ui.EndDisabled();
	ui.EndDisabled();

	// 外部から選ばれた子を表示するときは、祖先の折りたたみを解除する。
	if (scrollToId_ != 0) {
		auto parent = scene.FindObject(scrollToId_)->parentId;
		for (std::size_t count = 0;
		     parent != 0 && count < scene.GetObjects().size(); ++count) {
			collapsed_.erase(parent);
			const auto *object = scene.FindObject(parent);
			if (!object)
				break;
			parent = object->parentId;
		}
	}
	const auto visibleIds = GetVisibleIds(scene);
	ui.BeginDisabled(visibleIds.empty());

	if (ui.Button("Invert Listed")) {
		result.selectionChanged |= InvertVisible(scene, selection);
	}

	ui.EndDisabled();

	ui.Separator();

	const auto &objects = scene.GetObjects();

	const bool drawObjects = ui.BeginChild("HierarchyObjects", 0.0f, 0.0f);

	if (drawObjects) {
		const auto acceptParent = [&](SceneObjectId parent) {
			if (!editable || !ImGui::BeginDragDropTarget())
				return;
			if (const auto *payload =
			        ImGui::AcceptDragDropPayload("EDITOR_HIERARCHY_OBJECTS")) {
				if (payload->Data && payload->DataSize > 0 &&
				    payload->DataSize % sizeof(SceneObjectId) == 0) {
					const auto *ids =
					    static_cast<const SceneObjectId *>(payload->Data);
					result.parentObjects.assign(
					    ids, ids + payload->DataSize / sizeof(SceneObjectId));
					result.parentId = parent;
					result.keepWorld = !ImGui::GetIO().KeyAlt;
					result.action = Action::REPARENT_OBJECTS;
				}
			}
			ImGui::EndDragDropTarget();
		};
		ui.Selectable("Scene Root###HierarchyRoot", false);
		acceptParent(0);
		const bool filtered = IsFiltered();
		std::set<SceneObjectId> hasChildren;
		for (const auto &[id, object] : objects) {
			(void)id;
			if (object.parentId)
				hasChildren.insert(object.parentId);
		}

		// 描画と範囲選択で同じ表示一覧を使用し、検索で隠れた項目を除く
		SceneObjectId keyboardFocusId = 0;
		result.selectionChanged |=
		    NavigateSelection(visibleIds, selection, keyboardFocusId);

		for (const SceneObjectId id : visibleIds) {
			const SceneObject &object = objects.at(id);
			const std::string_view name = object.name.empty()
			                                  ? std::string_view{"Object"}
			                                  : std::string_view{object.name};

			// アクティブ物体に印を付けても、項目のIDは変えない
			const auto label = std::format(
			    "{}{}{}{}{}###SceneObject{}",
			    selection.GetActiveId() == id ? "> " : "  ",
			    !SceneActivity::IsActive(scene, id)
			        ? (object.active ? "[Inactive Parent] " : "[Inactive] ")
			        : "",
			    !object.IsHidden() ? "" : "[Hidden] ",
			    std::find(scenePickingDisabled_.begin(),
			              scenePickingDisabled_.end(),
			              id) != scenePickingDisabled_.end()
			        ? "[No Scene Pick] "
			        : "",
			    name, id);

			const auto &io = ImGui::GetIO();

			float indent = 0;
			if (!filtered) {
				auto parent = object.parentId;
				for (std::size_t depth = 0;
				     parent != 0 && depth < objects.size(); ++depth) {
					const auto *ancestor = scene.FindObject(parent);
					if (!ancestor)
						break;
					indent += ImGui::GetStyle().IndentSpacing;
					parent = ancestor->parentId;
				}
			}
			if (indent > 0)
				ImGui::Indent(indent);
			const bool branch = !filtered && hasChildren.contains(id);
			auto flags = ImGuiTreeNodeFlags_NoTreePushOnOpen |
			             ImGuiTreeNodeFlags_SpanAvailWidth |
			             ImGuiTreeNodeFlags_OpenOnArrow;
			if (!branch)
				flags |= ImGuiTreeNodeFlags_Leaf;
			if (selection.Contains(id))
				flags |= ImGuiTreeNodeFlags_Selected;
			ImGui::SetNextItemOpen(!collapsed_.contains(id), ImGuiCond_Always);
			const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
			if (branch && ImGui::IsItemToggledOpen()) {
				if (open)
					collapsed_.erase(id);
				else
					collapsed_.insert(id);
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left) &&
			    !ImGui::IsItemToggledOpen()) {
				const bool changed = SelectObject(id, visibleIds, io.KeyCtrl,
				                                  io.KeyShift, selection);
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

			if ((!object.IsHidden() || !object.HasMesh()) &&
			    ImGui::IsItemHovered() &&
			    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
			    !io.KeyCtrl && !io.KeyShift && !io.KeyAlt) {
				result.selectionChanged |=
				    SelectObject(id, visibleIds, false, false, selection);
				result.action = Action::FRAME_SELECTED;
				scrollToId_ = 0;
			}

			if (editable && ImGui::BeginDragDropSource()) {
				const auto selectedIds = selection.GetIds();
				if (selection.Contains(id)) {
					ImGui::SetDragDropPayload(
					    "EDITOR_HIERARCHY_OBJECTS", selectedIds.data(),
					    selectedIds.size_bytes(), ImGuiCond_Once);
				} else {
					ImGui::SetDragDropPayload("EDITOR_HIERARCHY_OBJECTS", &id,
					                          sizeof(id), ImGuiCond_Once);
				}
				ui.Text(std::format("{}", name));
				ui.Text(ImGui::GetIO().KeyAlt
				            ? "Keep Local Transform"
				            : "Keep World Transform (Alt: Keep Local)");
				ImGui::EndDragDropSource();
			}
			acceptParent(id);

			if (ImGui::BeginPopupContextItem()) {
				// 選択済みの項目ではグループを保ち、未選択の項目だけ選び直す
				if (!selection.Contains(id)) {
					result.selectionChanged |= selection.SelectOnly(id);
					rangeAnchorId_ = id;
					scrollToId_ = 0;
				}

				if (ui.MenuItem("Unparent (Keep World)", nullptr,
				                editable && object.parentId != 0)) {
					result.action = Action::REPARENT_OBJECTS;
					result.parentObjects.assign(selection.GetIds().begin(),
					                            selection.GetIds().end());
					result.parentId = 0;
					result.keepWorld = true;
				}
				if (ui.MenuItem("Unparent (Keep Local)", nullptr,
				                editable && object.parentId != 0)) {
					result.action = Action::REPARENT_OBJECTS;
					result.parentObjects.assign(selection.GetIds().begin(),
					                            selection.GetIds().end());
					result.parentId = 0;
					result.keepWorld = false;
				}

				if (ui.MenuItem("Copy", "Ctrl+C", editable)) {
					result.action = Action::COPY_OBJECTS;
				}

				if (ui.MenuItem("Frame Selected", "F", true)) {
					result.action = Action::FRAME_SELECTED;
				}

				const bool hasFollowers =
				    !FollowCameraUsage::FindObjects(scene, selection.GetIds())
				         .empty();

				if (ui.MenuItem("Select Follow Cameras", nullptr,
				                hasFollowers)) {
					result.action = Action::SELECT_FOLLOW_CAMERAS;
				}

				ui.BeginDisabled(!editable);

				bool canDisablePicking = false;
				bool canEnablePicking = false;

				for (const auto selectedId : selection.GetIds()) {
					const bool disabled =
					    std::find(scenePickingDisabled_.begin(),
					              scenePickingDisabled_.end(),
					              selectedId) != scenePickingDisabled_.end();
					canDisablePicking |= !disabled;
					canEnablePicking |= disabled;
				}

				if (ui.MenuItem("Disable Scene Picking", nullptr,
				                canDisablePicking) &&
				    editable &&
				    SetScenePickingDisabled(scene, selection.GetIds(), true)) {
					result.selectionChanged |= selection.Clear();
					rangeAnchorId_ = 0;
					scrollToId_ = 0;
				}

				if (ui.MenuItem("Enable Scene Picking", nullptr,
				                canEnablePicking) &&
				    editable) {
					SetScenePickingDisabled(scene, selection.GetIds(), false);
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

				if (ui.MenuItem("Duplicate with Offset...", nullptr, true)) {
					result.action = Action::DUPLICATE_ARRAY;
				}

				if (ui.MenuItem("Paste Box Colliders", nullptr,
				                canPasteCollider &&
				                    selection.GetCount() != 0)) {
					result.action = Action::PASTE_COLLIDERS;
				}

				if (ui.MenuItem("Paste Camera Lenses", nullptr,
				                canPasteCameraLens &&
				                    selection.GetCount() != 0)) {
					result.action = Action::PASTE_CAMERA_LENSES;
				}

				if (ui.MenuItem("Paste Respawn Settings", nullptr,
				                canPasteRespawn && selection.GetCount() != 0)) {
					result.action = Action::PASTE_RESPAWN_SETTINGS;
				}

				bool canHide = false;
				bool canShow = false;
				bool canEnable = false, canDisable = false;
				bool canGround = false;

				for (const auto selectedId : selection.GetIds()) {
					if (const auto *selected = scene.FindObject(selectedId)) {
						canHide |=
						    selected->meshRenderer && !selected->IsHidden();
						canShow |= selected->IsHidden();
						canEnable |= !selected->active;
						canDisable |= selected->active;
						canGround |= selected->IsRenderable();
					}
				}

				if (ui.MenuItem("Enable Selected", nullptr, canEnable)) {
					result.action = Action::ENABLE_OBJECTS;
				}
				if (ui.MenuItem("Disable Selected", nullptr, canDisable)) {
					result.action = Action::DISABLE_OBJECTS;
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

				if (ui.MenuItem("Snap Positions to Grid", nullptr, true)) {
					result.action = Action::SNAP_POSITIONS;
				}

				if (ui.MenuItem("Drop to Ground", "Shift+G", canGround)) {
					result.action = Action::DROP_TO_GROUND;
				}

				if (ui.MenuItem("Drop Colliders to Surface", nullptr,
				                ColliderPlacement::CanDrop(
				                    scene, selection.GetIds()))) {
					result.action = Action::DROP_TO_SURFACE;
				}

				if (ui.MenuItem("Place Colliders on Work Plane", nullptr,
				                ColliderPlacement::CanDrop(
				                    scene, selection.GetIds()))) {
					result.action = Action::PLACE_ON_WORK_PLANE;
				}

				if (ui.MenuItem("Align X to Active", nullptr,
				                selection.GetCount() > 1)) {
					result.action = Action::ALIGN_X;
				}

				if (ui.MenuItem("Align Y to Active", nullptr,
				                selection.GetCount() > 1)) {
					result.action = Action::ALIGN_Y;
				}

				if (ui.MenuItem("Align Z to Active", nullptr,
				                selection.GetCount() > 1)) {
					result.action = Action::ALIGN_Z;
				}

				ui.BeginDisabled(
				    selection.GetCount() < 2 ||
				    !ColliderPlacement::CanDrop(scene, selection.GetIds()));

				if (ui.BeginMenu("Align Collider Bounds")) {
					if (ui.MenuItem("Min X to Active", nullptr, true)) {
						result.action = Action::ALIGN_COLLIDER_MIN_X;
					}

					if (ui.MenuItem("Max X to Active", nullptr, true)) {
						result.action = Action::ALIGN_COLLIDER_MAX_X;
					}

					if (ui.MenuItem("Min Y to Active", nullptr, true)) {
						result.action = Action::ALIGN_COLLIDER_MIN_Y;
					}

					if (ui.MenuItem("Max Y to Active", nullptr, true)) {
						result.action = Action::ALIGN_COLLIDER_MAX_Y;
					}

					if (ui.MenuItem("Min Z to Active", nullptr, true)) {
						result.action = Action::ALIGN_COLLIDER_MIN_Z;
					}

					if (ui.MenuItem("Max Z to Active", nullptr, true)) {
						result.action = Action::ALIGN_COLLIDER_MAX_Z;
					}

					ui.EndMenu();
				}

				ui.EndDisabled();

				ui.BeginDisabled(
				    selection.GetCount() < 3 ||
				    !ColliderPlacement::CanDrop(scene, selection.GetIds()));

				if (ui.BeginMenu("Distribute Collider Gaps")) {
					if (ui.MenuItem("Equal Gaps X", nullptr, true)) {
						result.action = Action::DISTRIBUTE_COLLIDER_X;
					}

					if (ui.MenuItem("Equal Gaps Y", nullptr, true)) {
						result.action = Action::DISTRIBUTE_COLLIDER_Y;
					}

					if (ui.MenuItem("Equal Gaps Z", nullptr, true)) {
						result.action = Action::DISTRIBUTE_COLLIDER_Z;
					}

					ui.EndMenu();
				}

				ui.EndDisabled();

				if (ui.MenuItem("Distribute X", nullptr,
				                selection.GetCount() > 2)) {
					result.action = Action::DISTRIBUTE_X;
				}

				if (ui.MenuItem("Distribute Y", nullptr,
				                selection.GetCount() > 2)) {
					result.action = Action::DISTRIBUTE_Y;
				}

				if (ui.MenuItem("Distribute Z", nullptr,
				                selection.GetCount() > 2)) {
					result.action = Action::DISTRIBUTE_Z;
				}

				ui.EndDisabled();
				ImGui::EndPopup();
			}
			if (indent > 0)
				ImGui::Unindent(indent);
		}

		if (visibleIds.empty()) {
			ui.Text(objects.empty() ? "No objects." : "No matching objects.");
		}

		if (ImGui::BeginPopupContextWindow(
		        "HierarchyBackground", ImGuiPopupFlags_MouseButtonRight |
		                                   ImGuiPopupFlags_NoOpenOverItems)) {
			ui.BeginDisabled(!editable);

			if (ui.MenuItem("Create Empty", nullptr, true)) {
				result.action = Action::CREATE_EMPTY;
			}

			if (ui.MenuItem("Create Box", nullptr, true)) {
				result.action = Action::CREATE_BOX;
			}

			if (ui.MenuItem("Camera from Scene View", nullptr, true)) {
				result.action = Action::CREATE_CAMERA;
			}

			ui.EndDisabled();
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
	filter_.Clear();
	collapsed_.clear();
	pickingDisabledOnly_ = false;
	scenePickingDisabled_.clear();
	sortMode_ = SortMode::CREATION;
	rangeAnchorId_ = 0;
	lastActiveId_ = 0;
	scrollToId_ = 0;
	revealRequested_ = false;
}

std::span<const SceneObjectId>
HierarchyPanel::GetScenePickingDisabledIds() const {
	return scenePickingDisabled_;
}

bool HierarchyPanel::SetScenePickingDisabled(const Scene &scene,
                                             std::span<const SceneObjectId> ids,
                                             bool disabled) {
	for (const auto id : ids) {
		if (id == 0 || !scene.Contains(id)) {
			return false;
		}
	}

	auto next = scenePickingDisabled_;

	for (const auto id : ids) {
		if (disabled) {
			if (std::find(next.begin(), next.end(), id) == next.end()) {
				next.push_back(id);
			}
		} else {
			std::erase(next, id);
		}
	}

	scenePickingDisabled_.swap(next);
	return true;
}

bool HierarchyPanel::MatchesFilters(SceneObjectId id,
                                    const SceneObject &object) const {
	return filter_.Matches(id, object) &&
	       (!pickingDisabledOnly_ ||
	        std::find(scenePickingDisabled_.begin(),
	                  scenePickingDisabled_.end(),
	                  id) != scenePickingDisabled_.end());
}

std::vector<SceneObjectId>
HierarchyPanel::GetVisibleIds(const Scene &scene) const {
	std::vector<SceneObjectId> ids;
	ids.reserve(scene.GetObjects().size());

	for (const auto &[id, object] : scene.GetObjects()) {
		if (MatchesFilters(id, object)) {
			ids.push_back(id);
		}
	}

	if (sortMode_ != SortMode::CREATION) {
		struct Item {
			SceneObjectId id;
			std::string_view name;
		};

		std::vector<Item> items;
		items.reserve(ids.size());

		for (const auto id : ids) {
			const auto &name = scene.GetObjects().at(id).name;
			items.push_back(
			    {id, name.empty() ? std::string_view{"Object"} : name});
		}

		std::sort(items.begin(), items.end(),
		          [&](const Item &a, const Item &b) {
			          const int order = CompareNames(a.name, b.name);

			          if (order == 0) {
				          return a.id < b.id;
			          }

			          return sortMode_ == SortMode::NAME_ASCENDING ? order < 0
			                                                       : order > 0;
		          });

		for (std::size_t i = 0; i < items.size(); ++i) {
			ids[i] = items[i].id;
		}
	}

	if (IsFiltered())
		return ids;

	std::vector<SceneObjectId> order;

	if (!SceneHierarchy::GetEvaluationOrder(scene, order))
		return ids;

	std::map<SceneObjectId, std::vector<SceneObjectId>> children;

	for (const auto id : ids)
		children[scene.FindObject(id)->parentId].push_back(id);

	std::vector<SceneObjectId> stack(children[0].rbegin(), children[0].rend());
	std::vector<SceneObjectId> result;

	while (!stack.empty()) {
		const auto id = stack.back();
		stack.pop_back();
		result.push_back(id);

		if (collapsed_.contains(id))
			continue;

		const auto &descendants = children[id];
		stack.insert(stack.end(), descendants.rbegin(), descendants.rend());
	}

	return result;
}

bool HierarchyPanel::IsFiltered() const {
	return !filter_.text.empty() || filter_.hiddenOnly ||
	       filter_.component != HierarchyFilter::Component::ALL ||
	       pickingDisabledOnly_;
}

bool HierarchyPanel::SelectAllVisible(const Scene &scene,
                                      EditorSelection &selection) {
	auto ids = GetVisibleIds(scene);
	const auto current = selection.GetIds();

	if (std::equal(current.begin(), current.end(), ids.begin(), ids.end())) {
		return false;
	}

	rangeAnchorId_ = ids.empty() ? 0 : ids.front();
	const bool changed = selection.Set(std::move(ids));
	lastActiveId_ = selection.GetActiveId();
	return changed;
}

bool HierarchyPanel::InvertVisible(const Scene &scene,
                                   EditorSelection &selection) {
	const auto visible = GetVisibleIds(scene);

	if (visible.empty()) {
		return false;
	}

	const auto current = selection.GetIds();
	const std::unordered_set<SceneObjectId> listed(visible.begin(),
	                                               visible.end());
	const std::unordered_set<SceneObjectId> selected(current.begin(),
	                                                 current.end());
	std::vector<SceneObjectId> next;
	next.reserve(current.size() + visible.size());

	for (const auto id : current) {
		if (scene.Contains(id) && !listed.contains(id)) {
			next.push_back(id);
		}
	}

	for (const auto id : visible) {
		if (!selected.contains(id)) {
			next.push_back(id);
		}
	}

	if (!selection.Set(std::move(next))) {
		return false;
	}

	rangeAnchorId_ = lastActiveId_ = selection.GetActiveId();
	scrollToId_ = listed.contains(lastActiveId_) ? lastActiveId_ : 0;
	return true;
}

bool HierarchyPanel::NavigateSelection(
    std::span<const SceneObjectId> visibleIds, EditorSelection &selection,
    SceneObjectId &focusId) {
	focusId = 0;
	const auto &io = ImGui::GetIO();

	if (io.AppFocusLost || io.WantTextInput || io.KeyAlt ||
	    ImGui::IsAnyItemActive() ||
	    (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0 ||
	    !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows |
	                            ImGuiFocusedFlags_NoPopupHierarchy) ||
	    ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
		return false;
	}

	for (const auto button : {ImGuiMouseButton_Left, ImGuiMouseButton_Right,
	                          ImGuiMouseButton_Middle}) {
		if (ImGui::IsMouseDown(button) || ImGui::IsMouseReleased(button)) {
			return false;
		}
	}

	ImGuiKey pressed = ImGuiKey_None;
	bool control = false;
	bool shift = false;
	const auto flags =
	    ImGuiInputFlags_Repeat | ImGuiInputFlags_RouteFromRootWindow;

	for (const auto key :
	     {ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Home, ImGuiKey_End}) {
		// 修飾キーを同時に押した最初のフレームにも届くよう、全ての組み合わせを登録する
		const bool plain = ImGui::Shortcut(key, flags);
		const bool range = ImGui::Shortcut(ImGuiMod_Shift | key, flags);
		const bool addRange =
		    ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | key, flags);

		if (pressed == ImGuiKey_None && (plain || range || addRange)) {
			pressed = key;
			control = addRange;
			shift = range || addRange;
		}
	}

	if (pressed == ImGuiKey_None || visibleIds.empty()) {
		return false;
	}

	const auto active = std::find(visibleIds.begin(), visibleIds.end(),
	                              selection.GetActiveId());
	std::size_t index = 0;

	if (pressed == ImGuiKey_End ||
	    (active == visibleIds.end() && pressed == ImGuiKey_UpArrow)) {
		index = visibleIds.size() - 1;
	} else if (pressed != ImGuiKey_Home && active != visibleIds.end()) {
		index = static_cast<std::size_t>(active - visibleIds.begin());

		if (pressed == ImGuiKey_UpArrow && index > 0) {
			--index;
		} else if (pressed == ImGuiKey_DownArrow &&
		           index + 1 < visibleIds.size()) {
			++index;
		}
	}

	focusId = visibleIds[index];
	scrollToId_ = focusId;
	return SelectObject(focusId, visibleIds, control, shift, selection);
}

bool HierarchyPanel::SelectObject(SceneObjectId objectId,
                                  std::span<const SceneObjectId> visibleIds,
                                  bool control, bool shift,
                                  EditorSelection &selection) {
	const auto clicked =
	    std::find(visibleIds.begin(), visibleIds.end(), objectId);

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
