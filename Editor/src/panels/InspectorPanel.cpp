#include "panels/InspectorPanel.h"

#include "assets/ModelAssetDragDrop.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "math/TransformUtils.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"

#include <imgui.h>

#include <DirectXMath.h>
#include <format>
#include <string>
#include <utility>

namespace {

/// <summary>
/// 直前の編集UIの開始・変更・終了を名前と履歴へ反映する
/// </summary>
/// <param name="history">編集内容を記録する履歴</param>
/// <param name="objectId">編集する物体のID</param>
/// <param name="edited">UIで編集した名前</param>
/// <param name="changed">UIが値を変更したか</param>
/// <param name="target">変更を反映する名前</param>
/// <returns>true:値を変更した false:変更なし</returns>
bool ApplyNameEdit(EditorHistory &history, SceneObjectId objectId,
                   std::string &edited, bool changed, std::string &target) {
	const bool activated = ImGui::IsItemActivated();
	const bool deactivated = ImGui::IsItemDeactivated();

	if (activated && !history.BeginRename(objectId)) {
		return false;
	}

	bool applied = false;

	if (changed && target != edited) {
		// 保存などで確定した後、同じ入力欄で編集を続ける場合にも対応する
		if (!history.IsEditing() && !history.BeginRename(objectId)) {
			return false;
		}

		target.swap(edited);
		applied = true;
	}

	// 別の項目へ操作が移った場合は、新しい編集を途中で確定しない
	if (deactivated && !ImGui::IsAnyItemActive()) {
		history.CommitEdit();
	}

	return applied;
}

/// <summary>
/// 直前の編集UIの開始・変更・終了を変形情報と履歴へ反映する
/// </summary>
/// <param name="history">編集内容を記録する履歴</param>
/// <param name="objectId">編集する物体のID</param>
/// <param name="before">UIへ渡す前の変形情報</param>
/// <param name="edited">UIで編集した変形情報</param>
/// <param name="changed">UIが値を変更したか</param>
/// <param name="target">変更を反映する変形情報</param>
/// <returns>true:値を変更した false:変更なし</returns>
bool ApplyTransformEdit(EditorHistory &history, SceneObjectId objectId,
                        const Transform &before, const Transform &edited,
                        bool changed, Transform &target) {
	const bool activated = ImGui::IsItemActivated();
	const bool deactivated = ImGui::IsItemDeactivated();

	if (activated && !history.BeginTransform(objectId, before)) {
		return false;
	}

	bool applied = false;

	if (changed && TransformUtils::IsValid(edited) &&
	    !TransformUtils::AreEqual(before, edited)) {
		// キーボード操作など、変更から始まる場合にも対応する
		if (!history.IsEditing() && !history.BeginTransform(objectId, before)) {
			return false;
		}

		target = edited;
		applied = true;
	}

	// 別の項目へ操作が移った場合は、新しい編集を途中で確定しない
	if (deactivated && !ImGui::IsAnyItemActive()) {
		history.CommitEdit();
	}

	return applied;
}

} // namespace

void InspectorPanel::RequestRename(SceneObjectId objectId) {
	renameObjectId_ = objectId;
}

const InspectorPanel::ModelRequest &InspectorPanel::GetModelRequest() const {
	return modelRequest_;
}

const std::filesystem::path &InspectorPanel::GetAssetRevealPath() const {
	return assetRevealPath_;
}

const InspectorPanel::VisibilityRequest &InspectorPanel::GetVisibilityRequest() const {
	return visibilityRequest_;
}

bool InspectorPanel::Draw(const ImGuiSystem &ui, Scene &scene,
                          SceneObjectId selectedObjectId,
                          EditorHistory &history) {
	modelRequest_ = {};
	assetRevealPath_.clear();
	visibilityRequest_ = {};
	const bool focusName = renameObjectId_ != 0 &&
	                       renameObjectId_ == selectedObjectId;
	if (focusName) {
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
	} else {
		renameObjectId_ = 0;
	}
	if (!ui.BeginWindow("Inspector")) {
		ui.EndWindow();
		return false;
	}

	SceneObject *object = scene.FindObject(selectedObjectId);

	if (!object) {
		renameObjectId_ = 0;
		rotationObjectId_ = 0;
		ui.Text("No object selected.");
		ui.EndWindow();
		return false;
	}

	bool changed = false;

	{
		std::string edited = object->name;

		// 名前や選択物体が変わっても、別の入力欄の編集状態を引き継がない
		const auto label = std::format("Name###ObjectName{}", selectedObjectId);

		if (focusName) {
			ImGui::SetKeyboardFocusHere();
			renameObjectId_ = 0;
		}
		const bool editedValue = ui.InputText(label.c_str(), edited);

		if (ApplyNameEdit(history, selectedObjectId, edited, editedValue,
		                  object->name)) {
			changed = true;
		}
	}

	ui.Text(std::format("ID: {}", selectedObjectId));
	bool visible = object->visible;
	if (ImGui::Checkbox("Visible", &visible)) {
		visibilityRequest_ = {selectedObjectId, visible};
	}
	ImGui::SetItemTooltip("Render this object's model. Hidden models are excluded from Scene picking.");
	ui.Separator();

	if (ui.CollapsingHeader("Transform")) {
		Transform &transform = object->transform;

		{
			const Transform before = transform;
			Transform edited = before;

			const bool editedValue = ui.DragFloat3(
			    "Position", edited.translation, 0.01f, -10000.0f, 10000.0f);

			if (ApplyTransformEdit(history, selectedObjectId, before, edited,
			                       editedValue, transform)) {
				changed = true;
			}
		}

		{
			const Transform before = transform;
			Transform edited = before;

			Transform displayed;
			displayed.rotation = displayedRotation_;
			Transform current;
			current.rotation = before.rotation;

			// 選択変更、Undo・Redo、ギズモ操作などの外部変更だけ再変換する
			if (rotationObjectId_ != selectedObjectId ||
			    !TransformUtils::AreEqual(displayed, current)) {
				const auto radians =
				    TransformUtils::RotationToEuler(before.rotation);

				rotationDegrees_ = {
				    DirectX::XMConvertToDegrees(radians.x),
				    DirectX::XMConvertToDegrees(radians.y),
				    DirectX::XMConvertToDegrees(radians.z),
				};
				rotationObjectId_ = selectedObjectId;
			}

			const bool editedValue = ui.DragFloat3(
			    "Rotation", rotationDegrees_, 0.5f, -360.0f, 360.0f, "%.1f");

			// 表示は度数、シーンの保持値は単位クォータニオン
			if (editedValue) {
				edited.rotation = TransformUtils::RotationFromEuler({
				    DirectX::XMConvertToRadians(rotationDegrees_.x),
				    DirectX::XMConvertToRadians(rotationDegrees_.y),
				    DirectX::XMConvertToRadians(rotationDegrees_.z),
				});
			}

			if (ApplyTransformEdit(history, selectedObjectId, before, edited,
			                       editedValue, transform)) {
				changed = true;
			}

			displayedRotation_ = transform.rotation;
		}

		{
			const Transform before = transform;
			Transform edited = before;

			const bool editedValue =
			    ui.DragFloat3("Scale", edited.scale, 0.01f, 0.001f, 1000.0f);

			if (ApplyTransformEdit(history, selectedObjectId, before, edited,
			                       editedValue, transform)) {
				changed = true;
			}
		}
	}

	if (ui.CollapsingHeader("Model")) {
		const auto label = std::format("{}###ModelAsset{}",
		    object->modelPath.empty() ? "Drop model here" : PathUtils::ToUtf8(object->modelPath.filename()),
		    selectedObjectId);
		if (ImGui::Button(label.c_str(), {ImGui::GetContentRegionAvail().x, 0.0f}) &&
		    !object->modelPath.empty()) {
			assetRevealPath_ = object->modelPath;
		}
		if (auto path = ModelAssetDragDrop::AcceptTarget(); !path.empty()) {
			modelRequest_ = {selectedObjectId, std::move(path)};
		}
		if (object->modelPath.empty()) {
			ImGui::SetItemTooltip("Drag a model from Assets to assign it.");
		} else {
			ImGui::SetItemTooltip("%s\nClick to reveal in Assets. Drop a model here to replace it.",
			                      PathUtils::ToUtf8(object->modelPath).c_str());
		}
		ImGui::BeginDisabled(object->modelPath.empty());
		if (ImGui::Button("Remove Model")) {
			modelRequest_ = {selectedObjectId, {}};
		}
		ImGui::EndDisabled();
	}

	ui.EndWindow();
	return changed;
}
