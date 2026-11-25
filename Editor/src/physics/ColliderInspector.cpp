#include "physics/ColliderInspector.h"

#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "physics/ColliderUtils.h"
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
               SceneObject &object, const Collider &edited, bool changed) {
	if (ImGui::IsItemActivated() && !history.BeginCollider(id)) {
		return false;
	}

	bool applied = false;
	AxisAlignedBox bounds;

	if (changed && object.collider && *object.collider != edited &&
	    ColliderUtils::GetWorldBounds(edited, scene, id, bounds)) {
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

	const bool open = ui.CollapsingHeader("Collider");
	ImGui::SetItemTooltip("Right-click for collider actions.");
	if (ImGui::BeginPopupContextItem("ColliderActions")) {
		AxisAlignedBox bounds;
		const bool hasBounds = ColliderUtils::GetWorldBounds(*object.collider, scene, id, bounds);
		if (ui.MenuItem("Copy Collider", nullptr, ColliderUtils::IsValid(*object.collider)))
			request = {Action::COPY, id};
		if (ui.MenuItem("Paste Collider", nullptr, canPasteCollider))
			request = {Action::PASTE, id};
		if (ui.MenuItem("Fit Collider to Model", nullptr, true))
			request = {Action::FIT_TO_MODEL, id};
		if (ui.MenuItem("Frame Collider", nullptr, hasBounds))
			request = {Action::FRAME, id};
		if (ui.MenuItem("Remove Collider", nullptr, true))
			changed |= history.SetCollider(id, std::nullopt);
		ImGui::EndPopup();
	}
	if (open && object.collider) {
		AxisAlignedBox bounds;

		if (!ColliderUtils::GetWorldBounds(*object.collider, scene, id,
		                                   bounds)) {
			ImGui::TextWrapped(
			    "The collider cannot be represented at this transform. Adjust "
			    "position or scale, or fit the collider again.");
		}

		auto edited = *object.collider;
		const auto shapeLabel = edited.shape == Collider::Shape::BOX ? "Box"
		                        : edited.shape == Collider::Shape::SPHERE
		                            ? "Sphere"
		                            : "Capsule";
		if (ImGui::BeginCombo("Collider Shape", shapeLabel)) {
			for (const auto shape :
			     {Collider::Shape::BOX, Collider::Shape::SPHERE,
			      Collider::Shape::CAPSULE}) {
				const auto name = shape == Collider::Shape::BOX ? "Box"
				                  : shape == Collider::Shape::SPHERE
				                      ? "Sphere"
				                      : "Capsule";
				Collider proposed = edited;
				proposed.shape = shape;
				AxisAlignedBox supportedBounds;
				const bool supported = ColliderUtils::GetWorldBounds(
				    proposed, scene, id, supportedBounds);
				ImGui::BeginDisabled(!supported);

				if (ImGui::Selectable(name, edited.shape == shape)) {
					edited.shape = shape;
					changed |= history.SetCollider(id, edited);
				}

				ImGui::EndDisabled();
				if (!supported &&
				    ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip(
					    "Use uniform world scale for Sphere and Capsule.");
			}

			ImGui::EndCombo();
		}

		edited = *object.collider;
		bool valueChanged = ui.DragFloat3("Collider Center", edited.center,
		                                  0.01f, -10000, 10000);
		changed |= ApplyEdit(history, scene, id, object, edited, valueChanged);
		edited = *object.collider;

		if (edited.shape == Collider::Shape::BOX) {
			// 内部の半寸法は保持し、UIではローカル座標の全体の寸法を扱う。
			// doubleで表示し、既存データの大きな半寸法を2倍しても桁あふれさせない。
			double size[] = {double(edited.halfExtents.x) * 2,
			                 double(edited.halfExtents.y) * 2,
			                 double(edited.halfExtents.z) * 2};
			const double minimum = 0.002, maximum = 20000.0;
			valueChanged = ImGui::DragScalarN(
			    "Collider Size", ImGuiDataType_Double, size, 3, 0.01f, &minimum,
			    &maximum, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			const bool validSize =
			    std::all_of(std::begin(size), std::end(size), [](double value) {
				    return std::isfinite(value) && value > 0.0 &&
				           value <=
				               double((std::numeric_limits<float>::max)()) * 2;
			    });

			if (valueChanged && validSize) {
				edited.halfExtents = {static_cast<float>(size[0] * 0.5),
				                      static_cast<float>(size[1] * 0.5),
				                      static_cast<float>(size[2] * 0.5)};
			}

			changed |= ApplyEdit(history, scene, id, object, edited,
			                     valueChanged && validSize);

		} else {
			valueChanged =
			    ImGui::DragFloat("Collider Radius", &edited.radius, .01f, .001f,
			                     10000.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			changed |=
			    ApplyEdit(history, scene, id, object, edited, valueChanged);
			edited = *object.collider;

			if (edited.shape == Collider::Shape::CAPSULE) {
				double height = double(edited.halfHeight) * 2;
				const double minimum = .002, maximum = 20000;
				valueChanged = ImGui::DragScalar(
				    "Capsule Cylinder Height", ImGuiDataType_Double, &height,
				    .01f, &minimum, &maximum, "%.3f",
				    ImGuiSliderFlags_AlwaysClamp);
				if (std::isfinite(height) && height > 0 && height <= maximum)
					edited.halfHeight = static_cast<float>(height * .5);
				changed |=
				    ApplyEdit(history, scene, id, object, edited, valueChanged);
			}
		}

		edited = *object.collider;

		if (ImGui::Checkbox("Is Trigger", &edited.isTrigger)) {
			changed |= history.SetCollider(id, edited);
		}

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

	}

	ImGui::PopID();
	ImGui::PopID();
	return changed;
}
