#include "panels/InspectorPanel.h"
#include "appearance/ColorTintInspector.h"
#include "appearance/TextureSlotInspector.h"
#include "scene/SceneActivity.h"

#include "assets/AssetDragDrop.h"
#include "camera/CameraInspector.h"
#include "gameplay/GameplayInspector.h"
#include "graphics/model/ModelManager.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "math/TransformUtils.h"
#include "physics/ColliderInspector.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"

#include <imgui.h>

#include <DirectXMath.h>
#include <algorithm>
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
	slotModelPath_.clear();
	slotModelRevision_ = 0;
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

bool InspectorPanel::Draw(
    const ImGuiSystem &ui, Scene &scene, SceneObjectId selectedObjectId,
    EditorHistory &history, bool canPasteTransform, ModelManager *models,
    const AssetDatabase *assets, bool canPasteCollider, bool canPasteCameraLens,
    bool canPasteCharacter, bool canPasteFollow, bool canPasteRespawn,
    std::span<const SceneObjectId> uvTargets, bool canPasteUv) {
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

	const bool windowVisible = ui.BeginWindow("Inspector");

	if (focusName || focusInspect) {
		ui.FocusWindow("Inspector");
	}

	if (!windowVisible) {
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

	bool active = object->active;
	if (ImGui::Checkbox("Active", &active)) {
		const SceneObjectId targets[]{inspectedObjectId};
		changed |= history.SetActive(targets, active);
		// グループ履歴の確定でシーンの物体を置き換えるため、参照を取得し直す。
		object = scene.FindObject(inspectedObjectId);
	}
	ui.Text(SceneActivity::IsActive(scene, inspectedObjectId)
	            ? "Active in Hierarchy: Yes"
	            : "Active in Hierarchy: No");
	ui.Text(std::format("ID: {}", inspectedObjectId));
	ui.Separator();

	const bool transformExpanded =
	    ui.CollapsingHeader("Local Transform###Transform");

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

	if (object->mesh && ui.CollapsingHeader("Mesh")) {
		const auto label = std::format(
		    "{}###MeshAsset{}",
		    object->mesh->modelPath.empty()
		        ? "Drop model here"
		        : PathUtils::ToUtf8(object->mesh->modelPath.filename()),
		    inspectedObjectId);

		if (ImGui::Button(label.c_str(),
		                  {ImGui::GetContentRegionAvail().x, 0.0f}) &&
		    !object->mesh->modelPath.empty()) {
			assetRevealPath_ = object->mesh->modelPath;
		}

		if (auto path = AssetDragDrop::AcceptTarget(AssetDragDrop::Kind::MODEL);
		    !path.empty()) {
			modelRequest_ = {inspectedObjectId, std::move(path)};
		}

		if (object->mesh->modelPath.empty()) {
			ImGui::SetItemTooltip("Drag a model from Assets to assign it.");
		} else {
			ImGui::SetItemTooltip(
			    "%s\nClick to reveal in Assets. Drop a model "
			    "here to replace it.",
			    PathUtils::ToUtf8(object->mesh->modelPath).c_str());
		}

		ImGui::BeginDisabled(object->mesh->modelPath.empty());

		if (ImGui::Button("Clear Mesh Reference")) {
			modelRequest_ = {inspectedObjectId, {}};
		}

		ImGui::EndDisabled();

		if (ui.Button("Remove Mesh")) {
			objectRequest_ = {ObjectAction::REMOVE_MESH, inspectedObjectId};
		}
	}

	const auto componentScope = std::to_string(inspectedObjectId);
	ImGui::PushID(componentScope.c_str());

	if (ui.Button("Add Component"))
		ImGui::OpenPopup("AddComponentMenu");

	if (ImGui::BeginPopup("AddComponentMenu")) {
		if (ui.MenuItem("Mesh", nullptr, !object->mesh)) {
			objectRequest_ = {ObjectAction::ADD_MESH, inspectedObjectId};
		}

		if (ui.MenuItem("Mesh Renderer", nullptr, !object->meshRenderer)) {
			objectRequest_ = {ObjectAction::ADD_MESH_RENDERER,
			                  inspectedObjectId};
		}

		if (ui.MenuItem("Box Collider", nullptr, !object->collider))
			colliderFitRequest_ = inspectedObjectId;

		if (ui.MenuItem("Paste Box Collider", nullptr,
		                canPasteCollider && !object->collider)) {
			objectRequest_ = {ObjectAction::PASTE_COLLIDER, inspectedObjectId};
		}

		if (ui.MenuItem("Color Tint", nullptr, !object->colorTint)) {
			changed |= history.SetColorTint(inspectedObjectId, ColorTint{});
		}

		if (ui.MenuItem("Character Controller", nullptr, !object->character)) {
			changed |=
			    history.SetCharacter(inspectedObjectId, CharacterSettings{});
		}

		if (ui.MenuItem("Paste Character Settings", nullptr,
		                canPasteCharacter && !object->character)) {
			objectRequest_ = {ObjectAction::PASTE_CHARACTER_SETTINGS,
			                  inspectedObjectId};
		}

		if (ui.MenuItem("Camera", nullptr, !object->camera)) {
			objectRequest_ = {ObjectAction::ADD_CAMERA, inspectedObjectId};
		}

		if (ui.MenuItem("Paste Camera Lens", nullptr,
		                canPasteCameraLens && !object->camera)) {
			objectRequest_ = {ObjectAction::PASTE_CAMERA_LENS,
			                  inspectedObjectId};
		}

		if (ui.MenuItem("Follow Camera", nullptr, !object->followCamera)) {
			changed |= history.SetFollowCamera(inspectedObjectId,
			                                   FollowCameraSettings{});
		}

		if (ui.MenuItem("Paste Follow Settings", nullptr,
		                canPasteFollow && !object->followCamera)) {
			objectRequest_ = {ObjectAction::PASTE_FOLLOW_SETTINGS,
			                  inspectedObjectId};
		}

		if (ui.MenuItem("Respawn", nullptr, !object->respawn)) {
			changed |= history.SetRespawn(inspectedObjectId, RespawnSettings{});
		}

		if (ui.MenuItem("Paste Respawn Settings", nullptr,
		                canPasteRespawn && !object->respawn)) {
			objectRequest_ = {ObjectAction::PASTE_RESPAWN_SETTINGS,
			                  inspectedObjectId};
		}

		ImGui::EndPopup();
	}

	ImGui::PopID();

	if (object->meshRenderer && ui.CollapsingHeader("Mesh Renderer")) {
		bool visible = object->meshRenderer->visible;

		if (ImGui::Checkbox("Visible", &visible)) {
			visibilityRequest_ = {inspectedObjectId, visible};
		}

		if (models && assets) {
			if (object->HasMesh() &&
			    !models->FindLoaded(object->mesh->modelPath) &&
			    (slotModelPath_ != object->mesh->modelPath ||
			     slotModelRevision_ != models->GetCacheRevision())) {
				auto attempted = object->mesh->modelPath;
				models->Load(attempted);
				slotModelPath_.swap(attempted);
				slotModelRevision_ = models->GetCacheRevision();
			}

			const bool canApplyUv =
			    !uvTargets.empty() &&
			    std::all_of(uvTargets.begin(), uvTargets.end(),
			                [&scene](SceneObjectId id) {
				                const auto *target = scene.FindObject(id);
				                return target && target->meshRenderer &&
				                       target->meshRenderer->IsValid();
			                });

			TextureSlotInspector::Request request;
			changed |= TextureSlotInspector{}.Draw(
			    ui, inspectedObjectId, *object, history, *models, *assets,
			    request, canApplyUv, canPasteUv);
			assetRevealPath_ = std::move(request.revealTexture);

			if (request.clearUnusedTextures) {
				objectRequest_ = {ObjectAction::CLEAR_UNUSED_TEXTURES,
				                  inspectedObjectId};
			} else if (request.copyUv) {
				objectRequest_ = {ObjectAction::COPY_UV, inspectedObjectId};
			} else if (request.pasteUvToSelection) {
				objectRequest_ = {ObjectAction::PASTE_UV_TO_SELECTION,
				                  inspectedObjectId};
			} else if (request.applyUvToSelection) {
				objectRequest_ = {ObjectAction::APPLY_UV_TO_SELECTION,
				                  inspectedObjectId};
			}
		}

		if (ui.Button("Remove Mesh Renderer")) {
			objectRequest_ = {ObjectAction::REMOVE_MESH_RENDERER,
			                  inspectedObjectId};
		}
	}

	changed |=
	    ColorTintInspector{}.Draw(ui, inspectedObjectId, *object, history);

	ColliderInspector::Request colliderRequest;
	changed |=
	    ColliderInspector{}.Draw(ui, scene, inspectedObjectId, *object, history,
	                             colliderRequest, canPasteCollider);

	if (colliderRequest.action == ColliderInspector::Action::COPY) {
		objectRequest_ = {ObjectAction::COPY_COLLIDER,
		                  colliderRequest.objectId};
	} else if (colliderRequest.action == ColliderInspector::Action::PASTE) {
		objectRequest_ = {ObjectAction::PASTE_COLLIDER,
		                  colliderRequest.objectId};
	} else if (colliderRequest.action ==
	           ColliderInspector::Action::FIT_TO_MODEL) {
		colliderFitRequest_ = colliderRequest.objectId;
	} else if (colliderRequest.action ==
	           ColliderInspector::Action::SELECT_OVERLAPS) {
		objectRequest_ = {ObjectAction::SELECT_COLLIDER_OVERLAPS,
		                  colliderRequest.objectId};
	} else if (colliderRequest.action ==
	           ColliderInspector::Action::SET_WORK_PLANE) {
		objectRequest_ = {ObjectAction::SET_WORK_PLANE_FROM_COLLIDER,
		                  colliderRequest.objectId};
	} else if (colliderRequest.action == ColliderInspector::Action::FRAME) {
		objectRequest_ = {ObjectAction::FRAME_COLLIDER,
		                  colliderRequest.objectId};
	}

	CameraInspector::Request cameraRequest;
	changed |= CameraInspector{}.Draw(ui, inspectedObjectId, *object, history,
	                                  cameraRequest, canPasteCameraLens);

	if (cameraRequest.action == CameraInspector::Action::PREVIEW_IN_GAME) {
		objectRequest_ = {ObjectAction::PREVIEW_CAMERA, cameraRequest.objectId};
	} else if (cameraRequest.action ==
	           CameraInspector::Action::PLAY_FROM_CAMERA) {
		objectRequest_ = {ObjectAction::PLAY_FROM_CAMERA,
		                  cameraRequest.objectId};
	} else if (cameraRequest.action == CameraInspector::Action::ALIGN_VIEW) {
		objectRequest_ = {ObjectAction::ALIGN_CAMERA_VIEW,
		                  cameraRequest.objectId};
	} else if (cameraRequest.action == CameraInspector::Action::MATCH_VIEW) {
		objectRequest_ = {ObjectAction::MATCH_CAMERA_VIEW,
		                  cameraRequest.objectId};
	} else if (cameraRequest.action == CameraInspector::Action::MAKE_PRIMARY) {
		objectRequest_ = {ObjectAction::MAKE_PRIMARY_CAMERA,
		                  cameraRequest.objectId};
	} else if (cameraRequest.action == CameraInspector::Action::COPY_LENS) {
		objectRequest_ = {ObjectAction::COPY_CAMERA_LENS,
		                  cameraRequest.objectId};
	} else if (cameraRequest.action == CameraInspector::Action::PASTE_LENS) {
		objectRequest_ = {ObjectAction::PASTE_CAMERA_LENS,
		                  cameraRequest.objectId};
	}

	SceneObjectId targetSelectionRequest = 0;
	changed |= gameplayInspector_.Draw(
	    ui, scene, inspectedObjectId, *object, history, targetSelectionRequest,
	    canPasteTransform, canPasteCharacter, canPasteFollow, canPasteRespawn);

	const auto characterRequest = gameplayInspector_.GetCharacterRequest();

	if (characterRequest.action == GameplayInspector::CharacterAction::COPY) {
		objectRequest_ = {ObjectAction::COPY_CHARACTER_SETTINGS,
		                  characterRequest.objectId};
	} else if (characterRequest.action ==
	           GameplayInspector::CharacterAction::PASTE) {
		objectRequest_ = {ObjectAction::PASTE_CHARACTER_SETTINGS,
		                  characterRequest.objectId};
	} else if (characterRequest.action ==
	           GameplayInspector::CharacterAction::SELECT_GROUND) {
		objectRequest_ = {ObjectAction::SELECT_GROUND_COLLIDER,
		                  characterRequest.objectId};
	}

	const auto followRequest = gameplayInspector_.GetFollowRequest();

	if (followRequest.action == GameplayInspector::FollowAction::COPY) {
		objectRequest_ = {ObjectAction::COPY_FOLLOW_SETTINGS,
		                  followRequest.objectId};
	} else if (followRequest.action == GameplayInspector::FollowAction::PASTE) {
		objectRequest_ = {ObjectAction::PASTE_FOLLOW_SETTINGS,
		                  followRequest.objectId};
	} else if (followRequest.action ==
	           GameplayInspector::FollowAction::FRAME_PATH) {
		objectRequest_ = {ObjectAction::FRAME_FOLLOW_PATH,
		                  followRequest.objectId};
	}

	const auto respawnRequest = gameplayInspector_.GetRespawnRequest();

	if (respawnRequest.action == GameplayInspector::RespawnAction::COPY) {
		objectRequest_ = {ObjectAction::COPY_RESPAWN_SETTINGS,
		                  respawnRequest.objectId};
	} else if (respawnRequest.action ==
	           GameplayInspector::RespawnAction::PASTE) {
		objectRequest_ = {ObjectAction::PASTE_RESPAWN_SETTINGS,
		                  respawnRequest.objectId};
	} else if (respawnRequest.action ==
	           GameplayInspector::RespawnAction::MOVE_TO_POSITION) {
		objectRequest_ = {ObjectAction::MOVE_TO_RESPAWN,
		                  respawnRequest.objectId};
	}

	if (targetSelectionRequest != 0) {
		objectRequest_ = {ObjectAction::SELECT_OBJECT, targetSelectionRequest};
	}

	if (const auto pasteId = gameplayInspector_.GetRespawnPasteRequest();
	    pasteId != 0) {
		objectRequest_ = {ObjectAction::PASTE_RESPAWN_POSITION, pasteId};
	}

	if (const auto jumpId = gameplayInspector_.GetJumpFrameRequest();
	    jumpId != 0) {
		objectRequest_ = {ObjectAction::FRAME_JUMP, jumpId};
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
	ui.Text(object->active ? "Active: Yes" : "Active: No");
	ui.Text(SceneActivity::IsActive(scene, selectedObjectId)
	            ? "Active in Hierarchy: Yes"
	            : "Active in Hierarchy: No");
	ui.Text(std::format("ID: {}", selectedObjectId));

	std::optional<Transform> copied;

	if (ui.CollapsingHeader("Local Transform###Transform")) {
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

	if (object->mesh && ui.CollapsingHeader("Mesh")) {
		ui.Text(object->HasMesh() ? PathUtils::ToUtf8(object->mesh->modelPath)
		                          : "None");
	}

	if (object->meshRenderer && ui.CollapsingHeader("Mesh Renderer")) {
		bool visible = object->meshRenderer->visible;
		ui.BeginDisabled(true);
		ui.Checkbox("Visible", visible);
		ui.EndDisabled();

		ui.Text(std::format("UV Tiling: {:.3f}, {:.3f}",
		                    object->meshRenderer->uvScale[0],
		                    object->meshRenderer->uvScale[1]));
		ui.Text(std::format("UV Offset: {:.3f}, {:.3f}",
		                    object->meshRenderer->uvOffset[0],
		                    object->meshRenderer->uvOffset[1]));

		for (const auto &[slot, path] : object->meshRenderer->textures.slots) {
			ui.Text(std::format("Slot {}: {}", slot, PathUtils::ToUtf8(path)));
		}
	}

	if (object->collider && ui.CollapsingHeader("Box Collider")) {
		auto collider = *object->collider;
		ui.BeginDisabled(true);
		ui.DragFloat3("Collider Center", collider.center, 0.01f, 0.0f, 0.0f);
		double size[] = {double(collider.halfExtents.x) * 2,
		                 double(collider.halfExtents.y) * 2,
		                 double(collider.halfExtents.z) * 2};
		ImGui::DragScalarN("Collider Size", ImGuiDataType_Double, size, 3,
		                   0.01f, nullptr, nullptr, "%.2f");
		ui.EndDisabled();
		ui.Text(collider.isTrigger ? "Is Trigger: Yes" : "Is Trigger: No");
		ui.Text(std::format("Collision Layer: {}", collider.filter.layer));
		ui.Text(std::format("Collides With: 0x{:08X}", collider.filter.mask));
	}

	if (object->character && ui.CollapsingHeader("Character Controller")) {
		auto settings = *object->character;
		ui.BeginDisabled(true);
		ImGui::DragFloat("Move Speed", &settings.moveSpeed);
		ImGui::DragFloat("Jump Speed", &settings.jumpSpeed);
		ImGui::DragFloat("Gravity", &settings.gravity);
		ImGui::DragFloat("Terminal Speed", &settings.terminalSpeed);
		ImGui::DragFloat("Coyote Time (s)", &settings.coyoteTime);
		ImGui::DragFloat("Jump Buffer Time (s)", &settings.jumpBufferTime);
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

	if (object->colorTint && ui.CollapsingHeader("Color Tint")) {
		auto color = object->colorTint->color;
		ui.BeginDisabled(true);
		ui.ColorEdit3("Color", color);
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
