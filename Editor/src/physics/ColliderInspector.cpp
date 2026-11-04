#include "physics/ColliderInspector.h"

#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "physics/BoxColliderUtils.h"
#include "scene/SceneObject.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <limits>
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
                             Request &request, bool canPasteCollider) const {
	request = {};

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

		auto edited = *object.collider;
		bool valueChanged = ui.DragFloat3("Collider Center", edited.center,
		                                  0.01f, -10000, 10000);
		changed |= ApplyEdit(history, id, object, edited, valueChanged);
		edited = *object.collider;
		// 内部の半寸法は保持し、UIではローカル座標の全体の寸法を扱う。
		// doubleで表示し、既存データの大きな半寸法を2倍しても桁あふれさせない。
		double size[] = {double(edited.halfExtents.x) * 2,
		                 double(edited.halfExtents.y) * 2,
		                 double(edited.halfExtents.z) * 2};
		const double minimum = 0.002, maximum = 20000.0;
		valueChanged = ImGui::DragScalarN("Collider Size", ImGuiDataType_Double,
		                                  size, 3, 0.01f, &minimum, &maximum,
		                                  "%.2f", ImGuiSliderFlags_AlwaysClamp);
		const bool validSize =
		    std::all_of(std::begin(size), std::end(size), [](double value) {
			    return std::isfinite(value) && value > 0.0 &&
			           value <= double((std::numeric_limits<float>::max)()) * 2;
		    });

		if (valueChanged && validSize) {
			edited.halfExtents = {static_cast<float>(size[0] * 0.5),
			                      static_cast<float>(size[1] * 0.5),
			                      static_cast<float>(size[2] * 0.5)};
		}

		changed |=
		    ApplyEdit(history, id, object, edited, valueChanged && validSize);

		ui.BeginDisabled(!BoxColliderUtils::IsValid(*object.collider));

		if (ui.Button("Copy Box Collider")) {
			request = {Action::COPY, id};
		}

		ui.EndDisabled();
		ui.SameLine();
		ui.BeginDisabled(!canPasteCollider);

		if (ui.Button("Paste Box Collider")) {
			request = {Action::PASTE, id};
		}

		ui.EndDisabled();

		if (ui.Button("Fit Collider to Model")) {
			request = {Action::FIT_TO_MODEL, id};
		}

		ImGui::SetItemTooltip(
		    "Use the model's local bounds. Empty objects use a unit box.");

		ui.BeginDisabled(!BoxColliderUtils::GetWorldBounds(
		    *object.collider, object.transform, bounds));

		if (ui.Button("Frame Collider")) {
			request = {Action::FRAME, id};
		}

		if (ui.Button("Select Overlapping Colliders")) {
			request = {Action::SELECT_OVERLAPS, id};
		}

		ui.EndDisabled();

		if (ui.Button("Remove Box Collider")) {
			changed |= history.SetCollider(id, std::nullopt);
		}
	}

	ImGui::PopID();
	ImGui::PopID();
	return changed;
}
