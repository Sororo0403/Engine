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
/// <param name="scene">物体と祖先の変形を参照するシーン</param>
/// <param name="id">編集対象ID</param>
/// <param name="object">変更を反映する物体</param>
/// <param name="edited">編集後の形状</param>
/// <param name="changed">入力欄の値が変わったか</param>
/// <returns>true:形状を変更した false:変更なし</returns>
bool ApplyEdit(EditorHistory &history, const Scene &scene, SceneObjectId id,
               SceneObject &object, const BoxCollider &edited, bool changed) {
	if (ImGui::IsItemActivated() && !history.BeginCollider(id)) {
		return false;
	}

	bool applied = false;
	AxisAlignedBox bounds;

	if (changed && object.collider && *object.collider != edited &&
	    BoxColliderUtils::GetWorldBounds(edited, scene, id, bounds)) {
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

bool ColliderInspector::Draw(const ImGuiSystem &ui, const Scene &scene,
                             SceneObjectId id, SceneObject &object,
                             EditorHistory &history, Request &request,
                             bool canPasteCollider) const {
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

		if (!BoxColliderUtils::GetWorldBounds(*object.collider, scene, id,
		                                      bounds)) {
			ImGui::TextWrapped(
			    "The collider cannot be represented at this transform. Adjust "
			    "position or scale, or fit the collider again.");
		}

		auto edited = *object.collider;
		bool valueChanged = ui.DragFloat3("Collider Center", edited.center,
		                                  0.01f, -10000, 10000);
		changed |= ApplyEdit(history, scene, id, object, edited, valueChanged);
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

		changed |= ApplyEdit(history, scene, id, object, edited,
		                     valueChanged && validSize);

		edited = *object.collider;
		const auto layerLabel = edited.filter.layer == 0
		                            ? std::string{"0: Default"}
		                            : std::to_string(edited.filter.layer);
		if (ImGui::BeginCombo("Collision Layer", layerLabel.c_str())) {
			for (std::uint32_t layer = 0; layer < CollisionFilter::kLayerCount;
			     ++layer) {
				const auto label = layer == 0 ? std::string{"0: Default"}
				                              : std::to_string(layer);
				if (ImGui::Selectable(label.c_str(),
				                      edited.filter.layer == layer)) {
					edited.filter.layer = layer;
					changed |= history.SetCollider(id, edited);
				}
			}
			ImGui::EndCombo();
		}

		edited = *object.collider;
		const auto maskLabel = edited.filter.mask == CollisionFilter::kAllLayers
		                           ? std::string{"Everything"}
		                       : edited.filter.mask == 0
		                           ? std::string{"Nothing"}
		                           : std::string{"Custom"};
		if (ImGui::BeginCombo("Collides With", maskLabel.c_str())) {
			if (ImGui::Selectable("Everything",
			                      edited.filter.mask ==
			                          CollisionFilter::kAllLayers)) {
				edited.filter.mask = CollisionFilter::kAllLayers;
				changed |= history.SetCollider(id, edited);
			}
			if (ImGui::Selectable("Nothing", edited.filter.mask == 0)) {
				edited.filter.mask = 0;
				changed |= history.SetCollider(id, edited);
			}
			ImGui::Separator();
			for (std::uint32_t layer = 0; layer < CollisionFilter::kLayerCount;
			     ++layer) {
				const auto bit = std::uint32_t{1} << layer;
				bool enabled = (edited.filter.mask & bit) != 0;
				const auto label = layer == 0 ? std::string{"0: Default"}
				                              : std::to_string(layer);
				if (ImGui::Checkbox(label.c_str(), &enabled)) {
					edited.filter.mask = enabled ? edited.filter.mask | bit
					                             : edited.filter.mask & ~bit;
					changed |= history.SetCollider(id, edited);
				}
			}
			ImGui::EndCombo();
		}

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

		ui.BeginDisabled(!BoxColliderUtils::GetWorldBounds(*object.collider,
		                                                   scene, id, bounds));

		if (ui.Button("Frame Collider")) {
			request = {Action::FRAME, id};
		}

		if (ui.Button("Use Top as Work Plane")) {
			request = {Action::SET_WORK_PLANE, id};
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
