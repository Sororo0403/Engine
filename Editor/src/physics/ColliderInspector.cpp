#include "physics/ColliderInspector.h"

#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "physics/BoxColliderUtils.h"
#include "scene/SceneObject.h"

#include <imgui.h>
#include <string>

namespace {
/// <summary>
/// 形状編集の開始・変更・終了を、単一のUndo操作として記録する
/// </summary>
/// <param name="history">編集履歴</param>
/// <param name="id">編集対象ID</param>
/// <param name="object">変更を反映する物体</param>
/// <param name="edited">編集後の形状</param>
/// <param name="changed">入力欄の値が変わったか</param>
/// <returns>true:形状を変更した false:変更なし</returns>
bool ApplyEdit(EditorHistory &history, SceneObjectId id, SceneObject &object,
               const BoxCollider &edited, bool changed) {
	if (ImGui::IsItemActivated() && !history.BeginCollider(id)) {
		return false;
	}
	bool applied = false;
	AxisAlignedBox bounds;
	if (changed && object.collider && *object.collider != edited &&
	    BoxColliderUtils::GetWorldBounds(edited, object.transform, bounds)) {
		if (!history.IsEditing() && !history.BeginCollider(id)) {
			return false;
		}
		object.collider = edited;
		applied = true;
	}
	if (ImGui::IsItemDeactivated() && !ImGui::IsAnyItemActive()) {
		history.CommitEdit();
	}
	return applied;
}
} // namespace

bool ColliderInspector::Draw(const ImGuiSystem &ui, SceneObjectId id,
                             SceneObject &object, EditorHistory &history,
                             SceneObjectId &fitRequest) const {
	if (!object.collider) {
		return false;
	}
	const auto scope = std::to_string(id);
	ImGui::PushID(scope.c_str());
	ImGui::PushID("ColliderInspector");
	bool changed = false;
	if (ui.CollapsingHeader("Box Collider")) {
		AxisAlignedBox bounds;
		if (!BoxColliderUtils::GetWorldBounds(*object.collider,
		                                      object.transform, bounds)) {
			ImGui::TextWrapped(
			    "The collider cannot be represented at this transform. Adjust "
			    "position or scale, or fit the collider again.");
		}
		ImGui::TextWrapped(
		    "The green box is the world-axis-aligned collision shape. Center "
		    "and Half Size use object-local coordinates.");
		auto edited = *object.collider;
		bool valueChanged = ui.DragFloat3("Collider Center", edited.center,
		                                  0.01f, -10000, 10000);
		changed |= ApplyEdit(history, id, object, edited, valueChanged);
		edited = *object.collider;
		valueChanged = ui.DragFloat3("Collider Half Size", edited.halfExtents,
		                             0.01f, 0.001f, 10000);
		changed |= ApplyEdit(history, id, object, edited, valueChanged);
		if (ui.Button("Fit Collider to Model")) {
			fitRequest = id;
		}
		ImGui::SetItemTooltip(
		    "Use the model's local bounds. Empty objects use a unit box.");
		if (ui.Button("Remove Box Collider")) {
			changed |= history.SetCollider(id, std::nullopt);
		}
	}
	ImGui::PopID();
	ImGui::PopID();
	return changed;
}
