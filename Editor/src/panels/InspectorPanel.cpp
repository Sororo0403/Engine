#include "panels/InspectorPanel.h"

#include "assets/ModelAssetDragDrop.h"
#include "camera/CameraInspector.h"
#include "gameplay/GameplayInspector.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "math/TransformUtils.h"
#include "physics/ColliderInspector.h"
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

void InspectorPanel::RequestInspect(SceneObjectId objectId) {
	lockedObjectId_ = 0;
	renameObjectId_ = 0;
	rotationObjectId_ = 0;
	inspectObjectId_ = objectId;
}

void InspectorPanel::RequestRename(SceneObjectId objectId) {
	inspectObjectId_ = 0;
	lockedObjectId_ = 0;
	renameObjectId_ = objectId;
}

void InspectorPanel::Clear() {
	gameplayInspector_.Clear();
	inspectObjectId_ = 0;
	lockedObjectId_ = 0;
	renameObjectId_ = 0;
	rotationObjectId_ = 0;
	rotationDegrees_ = {};
	displayedRotation_ = {0.0f, 0.0f, 0.0f, 1.0f};
	modelRequest_ = {};
	visibilityRequest_ = {};
	objectRequest_ = {};
	colliderFitRequest_ = 0;
	assetRevealPath_.clear();
}

const InspectorPanel::ObjectRequest &InspectorPanel::GetObjectRequest() const {
	return objectRequest_;
}

const InspectorPanel::ModelRequest &InspectorPanel::GetModelRequest() const {
	return modelRequest_;
}

const std::filesystem::path &InspectorPanel::GetAssetRevealPath() const {
	return assetRevealPath_;
}

const InspectorPanel::VisibilityRequest &
InspectorPanel::GetVisibilityRequest() const {
	return visibilityRequest_;
}

SceneObjectId InspectorPanel::GetColliderFitRequest() const {
	return colliderFitRequest_;
}

bool InspectorPanel::Draw(const ImGuiSystem &ui, Scene &scene,
                          SceneObjectId selectedObjectId,
                          EditorHistory &history, bool canPasteTransform) {
	objectRequest_ = {};
	colliderFitRequest_ = 0;
	modelRequest_ = {};
	assetRevealPath_.clear();
	visibilityRequest_ = {};

	if (lockedObjectId_ != 0 && !scene.Contains(lockedObjectId_)) {
		lockedObjectId_ = 0;
	}

	const bool focusName =
	    renameObjectId_ != 0 && renameObjectId_ == selectedObjectId;
	const bool focusInspect = inspectObjectId_ != 0 &&
	                          inspectObjectId_ == selectedObjectId &&
	                          scene.Contains(inspectObjectId_);
	inspectObjectId_ = 0;

	if (focusName || focusInspect) {
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
	} else {
		renameObjectId_ = 0;
	}

	if (!ui.BeginWindow("Inspector")) {
		ui.EndWindow();
		return false;
	}

	const auto previousTarget =
	    lockedObjectId_ != 0 ? lockedObjectId_ : selectedObjectId;
	bool locked = lockedObjectId_ != 0;
	ImGui::BeginDisabled(!scene.Contains(previousTarget));

	if (ImGui::Checkbox("Lock Inspector", &locked)) {
		history.CommitEdit();
		lockedObjectId_ = locked ? previousTarget : 0;
		rotationObjectId_ = 0;
	}

	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Keep editing this object when selection changes. F2 "
	                      "for one selected object unlocks Inspector.");
	const SceneObjectId inspectedObjectId =
	    lockedObjectId_ != 0 ? lockedObjectId_ : selectedObjectId;
	SceneObject *object = scene.FindObject(inspectedObjectId);

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
		const auto label =
		    std::format("Name###ObjectName{}", inspectedObjectId);

		if (focusName) {
			ImGui::SetKeyboardFocusHere();
			renameObjectId_ = 0;
		}

		const bool editedValue = ui.InputText(label.c_str(), edited);

		if (ApplyNameEdit(history, inspectedObjectId, edited, editedValue,
		                  object->name)) {
			changed = true;
		}
	}

	ui.Text(std::format("ID: {}", inspectedObjectId));
	bool visible = object->visible;

	if (ImGui::Checkbox("Visible", &visible)) {
		visibilityRequest_ = {inspectedObjectId, visible};
	}

	ImGui::SetItemTooltip("Render this object's model. Hidden models are "
	                      "excluded from Scene picking.");
	ui.Separator();

	const bool transformExpanded = ui.CollapsingHeader("Transform");

	if (ImGui::BeginPopupContextItem("TransformTools")) {
		ImGui::TextDisabled("Inspected object only");

		if (ui.MenuItem("Copy Transform", nullptr, true)) {
			objectRequest_ = {ObjectAction::COPY_TRANSFORM, inspectedObjectId};
		}

		if (ui.MenuItem("Paste Transform", nullptr, canPasteTransform)) {
			objectRequest_ = {ObjectAction::PASTE_TRANSFORM, inspectedObjectId};
		}

		if (ui.MenuItem("Reset Transform", nullptr, true)) {
			objectRequest_ = {ObjectAction::RESET_TRANSFORM, inspectedObjectId};
		}

		ImGui::EndPopup();
	}

	if (transformExpanded) {
		Transform &transform = object->transform;

		{
			const Transform before = transform;
			Transform edited = before;

			const bool editedValue = ui.DragFloat3(
			    "Position", edited.translation, 0.01f, -10000.0f, 10000.0f);

			if (ApplyTransformEdit(history, inspectedObjectId, before, edited,
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
			if (rotationObjectId_ != inspectedObjectId ||
			    !TransformUtils::AreEqual(displayed, current)) {
				const auto radians =
				    TransformUtils::RotationToEuler(before.rotation);

				rotationDegrees_ = {
				    DirectX::XMConvertToDegrees(radians.x),
				    DirectX::XMConvertToDegrees(radians.y),
				    DirectX::XMConvertToDegrees(radians.z),
				};
				rotationObjectId_ = inspectedObjectId;
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

			if (ApplyTransformEdit(history, inspectedObjectId, before, edited,
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

			if (ApplyTransformEdit(history, inspectedObjectId, before, edited,
			                       editedValue, transform)) {
				changed = true;
			}
		}
	}

	if (ui.CollapsingHeader("Model")) {
		const auto label =
		    std::format("{}###ModelAsset{}",
		                object->modelPath.empty()
		                    ? "Drop model here"
		                    : PathUtils::ToUtf8(object->modelPath.filename()),
		                inspectedObjectId);

		if (ImGui::Button(label.c_str(),
		                  {ImGui::GetContentRegionAvail().x, 0.0f}) &&
		    !object->modelPath.empty()) {
			assetRevealPath_ = object->modelPath;
		}

		if (auto path = ModelAssetDragDrop::AcceptTarget(); !path.empty()) {
			modelRequest_ = {inspectedObjectId, std::move(path)};
		}

		if (object->modelPath.empty()) {
			ImGui::SetItemTooltip("Drag a model from Assets to assign it.");
		} else {
			ImGui::SetItemTooltip("%s\nClick to reveal in Assets. Drop a model "
			                      "here to replace it.",
			                      PathUtils::ToUtf8(object->modelPath).c_str());
		}

		ImGui::BeginDisabled(object->modelPath.empty());

		if (ImGui::Button("Remove Model")) {
			modelRequest_ = {inspectedObjectId, {}};
		}

		ImGui::EndDisabled();
	}

	const auto componentScope = std::to_string(inspectedObjectId);
	ImGui::PushID(componentScope.c_str());

	if (ui.Button("Add Component"))
		ImGui::OpenPopup("AddComponentMenu");

	if (ImGui::BeginPopup("AddComponentMenu")) {
		if (ui.MenuItem("Box Collider", nullptr, !object->collider))
			colliderFitRequest_ = inspectedObjectId;

		if (ui.MenuItem("Character Controller", nullptr, !object->character)) {
			changed |=
			    history.SetCharacter(inspectedObjectId, CharacterSettings{});
		}

		if (ui.MenuItem("Camera", nullptr, !object->camera)) {
			objectRequest_ = {ObjectAction::ADD_CAMERA, inspectedObjectId};
		}

		if (ui.MenuItem("Follow Camera", nullptr, !object->followCamera)) {
			changed |= history.SetFollowCamera(inspectedObjectId,
			                                   FollowCameraSettings{});
		}

		if (ui.MenuItem("Respawn", nullptr, !object->respawn)) {
			changed |= history.SetRespawn(inspectedObjectId, RespawnSettings{});
		}

		ImGui::EndPopup();
	}

	ImGui::PopID();

	ColliderInspector::Request colliderRequest;
	changed |= ColliderInspector{}.Draw(ui, inspectedObjectId, *object, history,
	                                    colliderRequest);

	if (colliderRequest.action == ColliderInspector::Action::FIT_TO_MODEL) {
		colliderFitRequest_ = colliderRequest.objectId;
	} else if (colliderRequest.action ==
	           ColliderInspector::Action::SELECT_OVERLAPS) {
		objectRequest_ = {ObjectAction::SELECT_COLLIDER_OVERLAPS,
		                  colliderRequest.objectId};
	}

	CameraInspector::Request cameraRequest;
	changed |= CameraInspector{}.Draw(ui, inspectedObjectId, *object, history,
	                                  cameraRequest);

	if (cameraRequest.action == CameraInspector::Action::ALIGN_VIEW) {
		objectRequest_ = {ObjectAction::ALIGN_CAMERA_VIEW,
		                  cameraRequest.objectId};
	} else if (cameraRequest.action == CameraInspector::Action::MAKE_PRIMARY) {
		objectRequest_ = {ObjectAction::MAKE_PRIMARY_CAMERA,
		                  cameraRequest.objectId};
	}

	SceneObjectId targetSelectionRequest = 0;
	changed |= gameplayInspector_.Draw(ui, scene, inspectedObjectId, *object,
	                                   history, targetSelectionRequest);

	if (targetSelectionRequest != 0) {
		objectRequest_ = {ObjectAction::SELECT_OBJECT, targetSelectionRequest};
	}

	if (const auto frameId = gameplayInspector_.GetRespawnFrameRequest();
	    frameId != 0) {
		objectRequest_ = {ObjectAction::FRAME_RESPAWN, frameId};
	}

	if (const auto blockerId = gameplayInspector_.GetRespawnBlockersRequest();
	    blockerId != 0) {
		objectRequest_ = {ObjectAction::SELECT_RESPAWN_BLOCKERS, blockerId};
	}

	ui.EndWindow();
	return changed;
}

std::optional<Transform>
InspectorPanel::DrawReadOnly(const ImGuiSystem &ui, const Scene &scene,
                             SceneObjectId selectedObjectId) const {
	if (!ui.BeginWindow("Inspector")) {
		ui.EndWindow();
		return std::nullopt;
	}

	const auto *object = scene.FindObject(selectedObjectId);

	if (!object) {
		ui.Text("No object selected.");
		ui.EndWindow();
		return std::nullopt;
	}

	ui.Text(std::format("Name: {}", object->name));
	ui.Text(std::format("ID: {}", selectedObjectId));
	bool visible = object->visible;
	ui.BeginDisabled(true);
	ui.Checkbox("Visible", visible);
	ui.EndDisabled();

	std::optional<Transform> copied;

	if (ui.CollapsingHeader("Transform")) {
		ui.BeginDisabled(!TransformUtils::IsValid(object->transform));

		if (ui.Button("Copy Transform")) {
			copied = object->transform;
		}

		ui.EndDisabled();

		auto transform = object->transform;
		auto rotation = TransformUtils::RotationToEuler(transform.rotation);
		rotation = {DirectX::XMConvertToDegrees(rotation.x),
		            DirectX::XMConvertToDegrees(rotation.y),
		            DirectX::XMConvertToDegrees(rotation.z)};
		ui.BeginDisabled(true);
		ui.DragFloat3("Position", transform.translation, 0.01f, 0.0f, 0.0f);
		ui.DragFloat3("Rotation", rotation, 0.1f, 0.0f, 0.0f);
		ui.DragFloat3("Scale", transform.scale, 0.01f, 0.0f, 0.0f);
		ui.EndDisabled();
	}

	if (!object->modelPath.empty()) {
		ImGui::TextWrapped("Model: %s",
		                   PathUtils::ToUtf8(object->modelPath).c_str());
	}

	if (object->collider && ui.CollapsingHeader("Box Collider")) {
		auto collider = *object->collider;
		ui.BeginDisabled(true);
		ui.DragFloat3("Collider Center", collider.center, 0.01f, 0.0f, 0.0f);
		ui.DragFloat3("Collider Half Size", collider.halfExtents, 0.01f, 0.0f,
		              0.0f);
		ui.EndDisabled();
	}

	if (object->character && ui.CollapsingHeader("Character Controller")) {
		auto settings = *object->character;
		ui.BeginDisabled(true);
		ImGui::DragFloat("Move Speed", &settings.moveSpeed);
		ImGui::DragFloat("Jump Speed", &settings.jumpSpeed);
		ImGui::DragFloat("Gravity", &settings.gravity);
		ImGui::DragFloat("Terminal Speed", &settings.terminalSpeed);
		ui.EndDisabled();
	}

	if (object->camera && ui.CollapsingHeader("Camera")) {
		auto camera = *object->camera;
		ui.BeginDisabled(true);
		ui.Checkbox("Primary", camera.primary);
		ImGui::DragFloat("Vertical FOV", &camera.settings.verticalFovDegrees);
		ImGui::DragFloat("Near Clip", &camera.settings.nearClip);
		ImGui::DragFloat("Far Clip", &camera.settings.farClip);
		ui.EndDisabled();
	}

	if (object->followCamera && ui.CollapsingHeader("Follow Camera")) {
		auto settings = *object->followCamera;
		const auto *target = scene.FindObject(settings.target);
		ui.Text(std::format("Target: {} (ID {})",
		                    target ? target->name : "None", settings.target));
		ui.BeginDisabled(true);
		ui.DragFloat3("Camera Offset", settings.offset, 0.01f, 0.0f, 0.0f);
		ui.DragFloat3("Look Offset", settings.lookOffset, 0.01f, 0.0f, 0.0f);
		ImGui::DragFloat("Follow Smoothing", &settings.smoothing);
		ui.EndDisabled();
	}

	if (object->respawn && ui.CollapsingHeader("Respawn")) {
		auto settings = *object->respawn;
		ui.BeginDisabled(true);
		ImGui::DragFloat("Fall Below Y", &settings.minimumY);
		ui.Checkbox("Use Start Position", settings.useStartPosition);

		if (!settings.useStartPosition) {
			ui.DragFloat3("Respawn Position", settings.position, 0.01f, 0.0f,
			              0.0f);
		}

		ui.EndDisabled();
	}

	ui.EndWindow();
	return copied;
}
