#include "panels/InspectorPanel.h"
#include "animation/AnimatorInspector.h"
#include "animation/AnimationClipAsset.h"
#include "animation/AnimationClipEditor.h"
#include "animation/TransformAnimationInspector.h"
#include "appearance/ColorTintInspector.h"
#include "appearance/TextureSlotInspector.h"
#include "gameplay/MotionPathInspector.h"
#include "lighting/PointLightInspector.h"
#include "lighting/SpotLightInspector.h"
#include "lighting/LightingControls.h"
#include "particles/ParticleEmitterInspector.h"
#include "physics/RigidBodyInspector.h"
#include "scene/SceneActivity.h"
#include "ui/UIInspector.h"

#include "assets/AssetDatabase.h"
#include "assets/AssetDragDrop.h"
#include "camera/CameraInspector.h"
#include "gameplay/GameplayInspector.h"
#include "gameplay/TriggerActionsInspector.h"
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
#include <cctype>
#include <format>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace {

/// <summary>
/// 表示名を大文字と小文字の区別なく検索する
/// </summary>
bool MatchesNameSearch(std::string_view name, std::string_view search) {
	return search.empty() ||
	       std::ranges::search(name, search, [](unsigned char a, unsigned char b) {
		       return std::tolower(a) == std::tolower(b);
	       }).begin() != name.end();
}

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
	tagDraft_.clear();
	componentSearch_.clear();
	animationBindingSearch_.clear();
	tagObjectId_ = 0;
	tagEditing_ = false;
	audioInspector_.Clear();
	gameplayInspector_.Clear();
	scriptInspector_.Clear();
	inspectObjectId_ = 0;
	lockedObjectId_ = 0;
	renameObjectId_ = 0;
	rotationObjectId_ = 0;
	rotationDegrees_ = {};
	displayedRotation_ = {0.0f, 0.0f, 0.0f, 1.0f};
	modelRequest_ = {};
	visibilityRequest_ = {};
	objectRequest_ = {};
	prefabRequest_ = {};
	scriptInspector_.ResetRequest();
	colliderFitRequest_ = 0;
	assetRevealPath_.clear();
	sceneOpenRequest_.clear();
	sceneOpenObject_ = 0;
	sceneLocationRequest_.clear();
	triggerActionsInspector_ = {};
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
    AssetDatabase *assets, bool canPasteCollider,
    std::span<const SceneObjectId> uvTargets, bool canPasteUv,
    std::span<const GameActionBinding> inputBindings) {
	objectRequest_ = {};
	prefabRequest_ = {};
	scriptInspector_.ResetRequest();
	colliderFitRequest_ = 0;
	modelRequest_ = {};
	assetRevealPath_.clear();
	sceneOpenRequest_.clear();
	sceneOpenObject_ = 0;
	sceneLocationRequest_.clear();
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

	if (tagObjectId_ != inspectedObjectId || !tagEditing_)
		tagDraft_ = object->tag;
	tagObjectId_ = inspectedObjectId;
	const auto tagLabel = std::format("Tag###ObjectTag{}", inspectedObjectId);
	ui.InputText(tagLabel.c_str(), tagDraft_);
	tagEditing_ = ImGui::IsItemActive();
	const bool validTag = SceneObject::IsValidTag(tagDraft_);
	if (!validTag)
		ImGui::SetItemTooltip("Tag must contain at most 64 UTF-8 bytes.");
	if (ImGui::IsItemDeactivatedAfterEdit() && tagDraft_ != object->tag &&
	    validTag) {
		history.CommitEdit();
		changed |= history.ApplySceneChanges(
		    [&](Scene &candidate) {
			    auto *edited = candidate.FindObject(inspectedObjectId);
			    if (!edited)
				    return false;
			    edited->tag = tagDraft_;
			    return true;
		    },
		    "Edit Object Tag");
		object = scene.FindObject(inspectedObjectId);
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

	prefabRequest_ = PrefabInspector{}.Draw(ui, scene, inspectedObjectId);
	changed |=
	    RigidBodyInspector{}.Draw(ui, inspectedObjectId, *object, history);
	changed |= UIInspector{}.Draw(
	    ui, inspectedObjectId, *object, history,
	    assets ? std::span<const std::filesystem::path>{assets->GetTextures()}
	           : std::span<const std::filesystem::path>{});
	changed |= scriptInspector_.Draw(
	    ui, scene, inspectedObjectId, *object, history,
	    sceneOpenRequest_,
	    assets ? std::span<const std::filesystem::path>{assets->GetScripts()}
	           : std::span<const std::filesystem::path>{},
	    assets ? std::span<const std::filesystem::path>{assets->GetPrefabs()}
	           : std::span<const std::filesystem::path>{},
	    assets ? std::span<const std::filesystem::path>{assets->GetSceneTargets()}
	           : std::span<const std::filesystem::path>{});
	changed |=
	    MotionPathInspector{}.Draw(ui, inspectedObjectId, *object, history);
	changed |= ParticleEmitterInspector{}.Draw(
	    ui, inspectedObjectId, *object, history,
	    assets ? std::span<const std::filesystem::path>{assets->GetTextures()}
	           : std::span<const std::filesystem::path>{});
	changed |= AnimatorInspector{}.Draw(ui, inspectedObjectId, *object, history,
	                                    models);
	changed |= TransformAnimationInspector{}.Draw(ui, inspectedObjectId,
	                                              *object, history);

	bool meshOpen = false;
	if (object->mesh) {
		meshOpen = ui.CollapsingHeader("Mesh");
		ImGui::SetItemTooltip("Right-click for mesh actions.");
		if (ImGui::BeginPopupContextItem("MeshActions")) {
			if (ui.MenuItem("Clear Mesh Reference", nullptr, !object->mesh->modelPath.empty()))
				modelRequest_ = {inspectedObjectId, {}};
			if (ui.MenuItem("Remove Mesh", nullptr, true))
				objectRequest_ = {ObjectAction::REMOVE_MESH, inspectedObjectId};
			ImGui::EndPopup();
		}
	}
	if (meshOpen) {
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

		auto mesh = *object->mesh;
		if (ImGui::Checkbox("Preload on Scene Load", &mesh.preload)) {
			changed |= history.SetMesh(inspectedObjectId, std::move(mesh));
		}

		ImGui::EndDisabled();
	}

	bool rendererOpen = false;
	if (object->meshRenderer) {
		rendererOpen = ui.CollapsingHeader("Mesh Renderer");
		ImGui::SetItemTooltip("Right-click for renderer actions.");
		if (ImGui::BeginPopupContextItem("MeshRendererActions")) {
			if (ui.MenuItem("Remove Mesh Renderer", nullptr, true))
				objectRequest_ = {ObjectAction::REMOVE_MESH_RENDERER, inspectedObjectId};
			ImGui::EndPopup();
		}
	}
	if (rendererOpen) {
		bool visible = object->meshRenderer->visible;

		if (ImGui::Checkbox("Visible", &visible)) {
			visibilityRequest_ = {inspectedObjectId, visible};
		}

		auto renderer = *object->meshRenderer;
		if (ImGui::Checkbox("Receive Fog", &renderer.receiveFog)) {
			changed |= history.SetMeshRenderer(inspectedObjectId, std::move(renderer));
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
			changed |= textureSlots_.Draw(
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
	} else if (colliderRequest.action == ColliderInspector::Action::FRAME) {
		objectRequest_ = {ObjectAction::FRAME_COLLIDER,
		                  colliderRequest.objectId};
	}

	changed |=
	    audioInspector_.Draw(ui, inspectedObjectId, *object, history, assets);
	if (!audioInspector_.GetRevealRequest().empty())
		assetRevealPath_ = audioInspector_.GetRevealRequest();

	std::optional<std::optional<AnimationClipComponent>> animationClipChange;
	if (object->animationClip && object->animationClip->usesAsset &&
	    ui.CollapsingHeader("Animation Player")) {
		auto player = *object->animationClip;
		const auto label = player.asset.empty() ? std::string("(None)")
		                                        : PathUtils::ToUtf8(player.asset.filename());
		const auto selectClip = [&](const std::filesystem::path &path) {
			if (player.asset != path) {
				player.asset = path;
				player.bindings.clear();
			}
		};

		if (ImGui::BeginCombo("Animation Clip", label.c_str())) {
			if (ImGui::Selectable("None", player.asset.empty()))
				selectClip({});
			if (assets)
				for (const auto &path : assets->GetAnimationClips()) {
					const auto name = PathUtils::ToUtf8(path.lexically_relative(
					    assets->GetRootDirectory()));
					if (ImGui::Selectable(name.c_str(), path == player.asset))
						selectClip(path);
				}
			ImGui::EndCombo();
		}
		if (const auto dropped = AssetDragDrop::AcceptTarget(AssetDragDrop::Kind::ANIMATION_CLIP);
		    !dropped.empty())
			selectClip(dropped);

		ImGui::Checkbox("Play on Start##AnimationPlayer", &player.playOnStart);
		ImGui::SameLine();
		ImGui::Checkbox("Loop##AnimationPlayer", &player.loop);
		if (!player.asset.empty() && ImGui::TreeNode("Clip Bindings")) {
			AnimationClipComponent assetClip;
			std::string error;
			if (AnimationClipAsset::Load(player.asset, assetClip, error)) {
				std::set<SceneObjectId> slots;
				for (const auto &track : assetClip.tracks)
					slots.insert(track.target);
				for (const auto slot : slots) {
					const auto canBind = [&](const SceneObject &candidate) {
						return std::ranges::all_of(
						    assetClip.tracks, [&](const auto &track) {
							    return track.target != slot || candidate.CanAnimate(track);
						    });
					};
					const auto binding = std::ranges::find_if(player.bindings, [&](const auto &entry) {
						return entry.first == slot;
					});
					const auto id = binding == player.bindings.end() ? 0 : binding->second;
					const auto *target = scene.FindObject(id);
					const bool incompatible = target && !canBind(*target);
					std::string preview = "(None)";
					if (target) {
						preview = target->name.empty() ? "Object" : target->name;
						if (incompatible) {
							preview = "Incompatible: " + preview;
						}
					} else if (id != 0) {
						preview = std::format("Missing object (ID {})", id);
					}
					ImGui::PushID(static_cast<int>(slot));
					const auto field = std::format("Binding {}", slot);
					const bool chooseTarget = ImGui::BeginCombo(
					    field.c_str(), preview.c_str(), ImGuiComboFlags_HeightLarge);
					if (incompatible) {
						ImGui::SetItemTooltip(
						    "%s\nThe target cannot animate every track in this binding.",
						    preview.c_str());
					} else if (!target && id != 0) {
						ImGui::SetItemTooltip("%s", preview.c_str());
					}
					if (chooseTarget) {
						if (ImGui::IsWindowAppearing()) {
							animationBindingSearch_.clear();
							ImGui::SetKeyboardFocusHere();
						}
						ui.InputText("Search", animationBindingSearch_);
						if (ImGui::Selectable("None", id == 0) &&
						    binding != player.bindings.end())
							binding->second = 0;
						bool found = false;
						for (const auto &[candidateId, candidate] : scene.GetObjects()) {
							const auto name = std::format("{} (ID {})",
							    candidate.name.empty() ? "Object" : candidate.name,
							    candidateId);
							if (!MatchesNameSearch(name, animationBindingSearch_))
								continue;
							if (!canBind(candidate))
								continue;
							found = true;
							const auto candidateLabel = name + "###" + std::to_string(candidateId);
							if (ImGui::Selectable(candidateLabel.c_str(), candidateId == id)) {
								if (binding == player.bindings.end())
									player.bindings.emplace_back(slot, candidateId);
								else
									binding->second = candidateId;
								break;
							}
						}
						if (!found) {
							ImGui::TextDisabled("No compatible objects match.");
						}
						ImGui::EndCombo();
					}
					ImGui::PopID();
				}
			} else {
				ui.Text(error);
			}
			ImGui::TreePop();
		}
		if (ui.Button("Remove Animation Player"))
			animationClipChange.emplace(std::nullopt);
		else if (player != *object->animationClip)
			animationClipChange.emplace(std::move(player));
	} else if (object->animationClip && !object->animationClip->usesAsset &&
	           ui.CollapsingHeader("Legacy Animation Clip")) {
		ImGui::Text("%.2f s  /  %zu tracks", object->animationClip->duration,
		            object->animationClip->tracks.size());
		if (ui.Button("Edit Timeline...")) {
			animationClipDraft_ = *object->animationClip;
			animationClipPlayhead_ = 0;
			ui.OpenPopup("Animation Clip");
		}
		const auto workSize = ImGui::GetMainViewport()->WorkSize;
		const ImVec2 maximumSize{(std::max)(1.f, workSize.x - 24.f),
		                        (std::max)(1.f, workSize.y - 24.f)};
		ImGui::SetNextWindowSize(
		    {(std::min)(700.f, maximumSize.x), (std::min)(540.f, maximumSize.y)},
		    ImGuiCond_Appearing);
		ImGui::SetNextWindowSizeConstraints(
		    {(std::min)(420.f, maximumSize.x), (std::min)(300.f, maximumSize.y)},
		    maximumSize);
		// トラック領域はウィンドウの残り高さを使うため、自動リサイズを使わない。
		if (ImGui::BeginPopupModal("Animation Clip", nullptr,
		                           ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::BeginChild("Timeline Tracks", {0, -ImGui::GetFrameHeightWithSpacing() * 2});
			AnimationClipEditor::DrawKeys(ui, &scene, animationClipDraft_, animationClipPlayhead_);
			ImGui::EndChild();
			const bool valid = animationClipDraft_.IsValid();
			if (!valid)
				ImGui::TextUnformatted("Use distinct key times within the duration and one track per target property.");
			ImGui::BeginDisabled(!valid);
			if (ui.Button("Apply Timeline")) {
				animationClipChange.emplace(animationClipDraft_);
				ui.CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ui.SameLine();
			if (ui.Button("Cancel"))
				ui.CloseCurrentPopup();
			ui.EndPopup();
		}
		ui.SameLine();
		if (ui.Button("Remove Animation Clip"))
			animationClipChange.emplace(std::nullopt);
	}

	std::optional<std::optional<DirectionalLightComponent>> directionalLightChange;
	if (object->directionalLight && ui.CollapsingHeader("Directional Light")) {

		if (ui.Button("Edit Directional Light...")) {
			directionalLightDraft_ = *object->directionalLight;
			ui.OpenPopup("Edit Directional Light");
		}

		if (ui.BeginPopupModal("Edit Directional Light")) {
			auto &draft = directionalLightDraft_;
			ImGui::ColorEdit3("Light Color", draft.color.data());
			ImGui::SliderFloat("Light Intensity", &draft.intensity, 0.0f,
			                   LightingSettings::kMaxIntensity);
			LightingControls::DrawShadows(draft.shadows);

			if (ui.Button("Apply") && draft.IsValid()) {
				directionalLightChange.emplace(draft);
				ui.CloseCurrentPopup();
			}

			ui.SameLine();
			if (ui.Button("Cancel")) {
				ui.CloseCurrentPopup();
			}
			ui.EndPopup();
		}

		ui.SameLine();
		if (ui.Button("Remove Directional Light")) {
			directionalLightChange.emplace(std::nullopt);
		}
	}

	changed |=
	    PointLightInspector{}.Draw(ui, inspectedObjectId, *object, history);
	changed |=
	    SpotLightInspector{}.Draw(ui, inspectedObjectId, *object, history);

	changed |= triggerActionsInspector_.Draw(
	    ui, scene, inspectedObjectId, *object, history,
	    sceneOpenRequest_, sceneOpenObject_, sceneLocationRequest_,
	    assets ? std::span<const std::filesystem::path>(assets->GetSceneTargets())
	           : std::span<const std::filesystem::path>{},
	    inputBindings);

	CameraInspector::Request cameraRequest;
	changed |= CameraInspector{}.Draw(ui, inspectedObjectId, *object, history,
	                                  cameraRequest);

	if (cameraRequest.action == CameraInspector::Action::PLAY_FROM_CAMERA) {
		objectRequest_ = {ObjectAction::PLAY_FROM_CAMERA,
		                  cameraRequest.objectId};
	} else if (cameraRequest.action == CameraInspector::Action::ALIGN_VIEW) {
		objectRequest_ = {ObjectAction::ALIGN_CAMERA_VIEW,
		                  cameraRequest.objectId};
	} else if (cameraRequest.action == CameraInspector::Action::MAKE_PRIMARY) {
		objectRequest_ = {ObjectAction::MAKE_PRIMARY_CAMERA,
		                  cameraRequest.objectId};
	}

	SceneObjectId targetSelectionRequest = 0;
	changed |= gameplayInspector_.Draw(
	    ui, scene, inspectedObjectId, *object, history, targetSelectionRequest);

	if (targetSelectionRequest != 0) {
		objectRequest_ = {ObjectAction::SELECT_OBJECT, targetSelectionRequest};
	}

	ui.Separator();

	const auto componentScope = std::to_string(inspectedObjectId);
	ImGui::PushID(componentScope.c_str());

	const bool openingComponents = ui.Button("Add Component");
	if (openingComponents) {
		componentSearch_.clear();
		ImGui::OpenPopup("AddComponentMenu");
	}

	if (ImGui::BeginPopup("AddComponentMenu")) {
		if (openingComponents) {
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::SetNextItemWidth(240.0f);
		ui.InputText("Search###ComponentSearch", componentSearch_);
		ui.Separator();

		bool hasMatches = false;
		const auto componentItem = [&](const char *name, bool enabled) {
			if (!MatchesNameSearch(name, componentSearch_)) {
				return false;
			}
			hasMatches = true;
			return ui.MenuItem(name, nullptr, enabled);
		};

		if (componentItem("Mesh", !object->mesh)) {
			objectRequest_ = {ObjectAction::ADD_MESH, inspectedObjectId};
		}

		if (componentItem("Mesh Renderer", !object->meshRenderer)) {
			objectRequest_ = {ObjectAction::ADD_MESH_RENDERER,
			                  inspectedObjectId};
		}

		if (componentItem("Collider", !object->collider))
			colliderFitRequest_ = inspectedObjectId;

		if (componentItem("Paste Collider",
		                  canPasteCollider && !object->collider)) {
			objectRequest_ = {ObjectAction::PASTE_COLLIDER, inspectedObjectId};
		}

		if (componentItem("Audio Source", !object->audioSource))
			changed |= history.SetAudioSource(inspectedObjectId,
			                                  AudioSourceComponent{});
		if (componentItem("Audio Listener", !object->audioListener))
			changed |= history.SetAudioListener(inspectedObjectId,
			                                    AudioListenerComponent{});

		if (componentItem("Directional Light", !object->directionalLight)) {
			directionalLightChange.emplace(DirectionalLightComponent{});
		}

		if (componentItem("Point Light", !object->pointLight)) {
			changed |=
			    history.SetPointLight(inspectedObjectId, PointLightComponent{});
		}

		if (componentItem("Canvas", !object->canvas && !object->rectTransform))
			changed |= history.SetCanvas(inspectedObjectId, CanvasComponent{});
		if (componentItem("Rect Transform",
		                  !object->rectTransform && !object->canvas))
			changed |= history.SetRectTransform(inspectedObjectId,
			                                    RectTransformComponent{});
		if (componentItem("UI Button",
		                  !object->button && object->rectTransform.has_value()))
			changed |= history.SetButton(inspectedObjectId, ButtonComponent{});
		if (componentItem("UI Text",
		                  !object->text && object->rectTransform.has_value()))
			changed |= history.SetText(inspectedObjectId, TextComponent{});
		if (componentItem("UI Image",
		                  !object->image && object->rectTransform.has_value()))
			changed |= history.SetImage(inspectedObjectId, ImageComponent{});
		if (componentItem("Rigid Body", !object->rigidBody))
			changed |=
			    history.SetRigidBody(inspectedObjectId, RigidBodySettings{});
		if (componentItem("Lua Script", !object->script))
			changed |= history.SetScript(inspectedObjectId, ScriptComponent{});

		if (componentItem("Animator", !object->animator))
			changed |=
			    history.SetAnimator(inspectedObjectId, AnimatorComponent{});
		if (componentItem("Particle Emitter", !object->particleEmitter))
			changed |= history.SetParticleEmitter(inspectedObjectId,
			                                      ParticleEmitterComponent{});
		if (componentItem("Transform Animation", !object->transformAnimation))
			changed |= history.SetTransformAnimation(
			    inspectedObjectId, TransformAnimationComponent{});
		if (componentItem("Motion Path", !object->motionPath)) {
			changed |=
			    history.SetMotionPath(inspectedObjectId, MotionPathComponent{});
		}

		if (componentItem("Spot Light", !object->spotLight)) {
			changed |=
			    history.SetSpotLight(inspectedObjectId, SpotLightComponent{});
		}

		if (componentItem("Color Tint", !object->colorTint)) {
			changed |= history.SetColorTint(inspectedObjectId, ColorTint{});
		}

		if (componentItem("Character Controller", !object->character)) {
			changed |=
			    history.SetCharacter(inspectedObjectId, CharacterSettings{});
		}

		if (componentItem("Camera", !object->camera)) {
			objectRequest_ = {ObjectAction::ADD_CAMERA, inspectedObjectId};
		}


		if (componentItem("Follow Camera", !object->followCamera)) {
			changed |= history.SetFollowCamera(inspectedObjectId,
			                                   FollowCameraSettings{});
		}

		if (componentItem("Animation Player", !object->animationClip)) {
			AnimationClipComponent player;
			player.usesAsset = true;
			animationClipChange.emplace(std::move(player));
		}

		if (componentItem("Trigger Actions", !object->triggerActions)) {
			changed |= history.SetTriggerActions(inspectedObjectId,
			                                     TriggerActionsComponent{});
		}

		if (componentItem("Respawn", !object->respawn)) {
			changed |= history.SetRespawn(inspectedObjectId, RespawnSettings{});
		}

		if (!hasMatches) {
			ui.Text("No matching components.");
		}
		ImGui::EndPopup();
	}

	ImGui::PopID();

	if (animationClipChange) {
		history.CommitEdit();
		changed |= history.ApplySceneChanges(
		    [&](Scene &candidate) {
			    auto *target = candidate.FindObject(inspectedObjectId);
			    if (!target)
				    return false;
			    target->animationClip = *animationClipChange;
			    return true;
		    },
		    "Edit Animation Player");
	}

	if (directionalLightChange) {
		history.CommitEdit();
		changed |= history.ApplySceneChanges(
		    [&](Scene &candidate) {
			    auto *edited = candidate.FindObject(inspectedObjectId);
			    if (!edited)
				    return false;

			    edited->directionalLight = *directionalLightChange;

			    if (edited->directionalLight &&
			        edited->directionalLight->shadows.enabled) {
				    // 影を有効にした平行光へ、影の担当を移す。
				    for (const auto &[id, other] : candidate.GetObjects()) {
					    if (id != inspectedObjectId && other.directionalLight &&
					        other.directionalLight->shadows.enabled) {
						    candidate.FindObject(id)->directionalLight->shadows.enabled =
						        false;
					    }
				    }
			    }

			    return true;
		    },
		    "Edit Directional Light");
	}

	ui.EndWindow();
	return changed;
}

std::optional<Transform> InspectorPanel::DrawReadOnly(
    const ImGuiSystem &ui, const Scene &scene, SceneObjectId selectedObjectId,
    std::optional<std::string_view> scriptState) const {
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
	if (!object->tag.empty())
		ui.Text(std::format("Tag: {}", object->tag));
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

	PrefabInspector{}.Draw(ui, scene, selectedObjectId, false);

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

	if (object->collider && ui.CollapsingHeader("Collider")) {
		auto collider = *object->collider;
		ui.BeginDisabled(true);
		ui.DragFloat3("Collider Center", collider.center, 0.01f, 0.0f, 0.0f);
		ui.Text(collider.shape == Collider::Shape::BOX      ? "Shape: Box"
		        : collider.shape == Collider::Shape::SPHERE ? "Shape: Sphere"
		                                                    : "Shape: Capsule");
		if (collider.shape == Collider::Shape::BOX) {
			double size[] = {double(collider.halfExtents.x) * 2,
			                 double(collider.halfExtents.y) * 2,
			                 double(collider.halfExtents.z) * 2};
			ImGui::DragScalarN("Collider Size", ImGuiDataType_Double, size, 3,
			                   0.01f, nullptr, nullptr, "%.2f");
		} else {
			ui.Text(std::format("Radius: {:.3f}", collider.radius));
			if (collider.shape == Collider::Shape::CAPSULE)
				ui.Text(std::format("Cylinder Height: {:.3f}",
				                    double(collider.halfHeight) * 2));
		}

		ui.EndDisabled();
		ui.Text(collider.isTrigger ? "Is Trigger: Yes" : "Is Trigger: No");
		ui.Text(std::format("Collision Layer: {}", collider.filter.layer));
		ui.Text(std::format("Collides With: 0x{:08X}", collider.filter.mask));
	}

	UIInspector{}.DrawReadOnly(ui, *object);
	RigidBodyInspector{}.DrawReadOnly(ui, *object);
	scriptInspector_.DrawReadOnly(ui, *object, scriptState);
	audioInspector_.DrawReadOnly(ui, *object);

	PointLightInspector{}.DrawReadOnly(ui, *object);
	SpotLightInspector{}.DrawReadOnly(ui, *object);
	if (object->directionalLight && ui.CollapsingHeader("Directional Light")) {
		ImGui::Text("Intensity: %.2f", object->directionalLight->intensity);
	}
	MotionPathInspector{}.DrawReadOnly(ui, *object);
	TransformAnimationInspector{}.DrawReadOnly(ui, *object);
	if (object->animationClip && ui.CollapsingHeader("Animation Clip")) {
		ImGui::Text("%.2f s  /  %zu tracks", object->animationClip->duration,
		            object->animationClip->tracks.size());
	}
	ParticleEmitterInspector{}.DrawReadOnly(ui, *object);
	AnimatorInspector{}.DrawReadOnly(ui, *object);

	TriggerActionsInspector{}.DrawReadOnly(ui, scene, *object);

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
		ui.Checkbox("Orbit Input", settings.orbit.enabled);

		if (settings.orbit.enabled) {
			ui.Text(std::format(
			    "Mouse: {:.2f} / Stick: {:.2f} deg/s / Zoom: {:.2f}",
			    settings.orbit.mouseSensitivity, settings.orbit.stickSpeed,
			    settings.orbit.zoomSensitivity));
			ui.Text(std::format(
			    "Pitch: {:.2f} to {:.2f} / Distance: {:.2f} to {:.2f}",
			    settings.orbit.minimumPitch, settings.orbit.maximumPitch,
			    settings.orbit.minimumDistance,
			    settings.orbit.maximumDistance));
		}

		ui.EndDisabled();
	}

	if (object->respawn && ui.CollapsingHeader("Respawn")) {
		auto settings = *object->respawn;
		ui.BeginDisabled(true);
		int axis = static_cast<int>(settings.axis);
		ImGui::Combo("Fall Axis", &axis, "X\0Y\0Z\0");
		ui.Checkbox("Respawn Above Boundary", settings.whenAbove);
		ImGui::DragFloat("Fall Boundary", &settings.threshold);
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
