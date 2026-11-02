#include "scene/EditorScene.h"

#include "assets/MaterialUsage.h"
#include "assets/ModelPlacement.h"
#include "assets/ModelUsage.h"
#include "camera/CameraObjects.h"
#include "camera/ObjectFrameBounds.h"
#include "core/EngineContext.h"
#include "core/Logger.h"
#include "gameplay/FollowCameraUsage.h"
#include "gameplay/PlayShortcuts.h"
#include "gameplay/SampleStage.h"
#include "graphics/model/ModelManager.h"
#include "graphics/render/RenderTexture.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "input/InputSystem.h"
#include "input/Key.h"
#include "math/MathUtils.h"
#include "math/Ray.h"
#include "math/TransformUtils.h"
#include "physics/BoxColliderUtils.h"
#include "physics/ColliderPlacement.h"
#include "physics/CollisionWorld.h"
#include "platform/windows/PathUtils.h"
#include "scene/PrimitiveObjects.h"
#include "scene/Scene.h"
#include "selection/SelectionFrustum.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <format>
#include <imgui.h>
#include <limits>
#include <utility>
#include <vector>

EditorScene::EditorScene(const EngineContext &context)
    : ui_(context.ui), input_(context.input), models_(context.models),
      renderer_(document_.GetScene(), context.models, context.render),
      playMode_(context.models, context.render),
      playSceneRenderer_(playMode_.GetSession().GetScene(), context.models,
                         context.render),
      sceneViewPanel_(context.render) {
}

EditorScene::~EditorScene() = default;

bool EditorScene::Initialize() {
	if (!ui_.EnableDocking()) {
		return false;
	}

	if (!renderer_.Initialize() || !playMode_.Initialize() ||
	    !playSceneRenderer_.Initialize()) {
		return false;
	}

	gizmo_.Cancel();
	gizmoEditActive_ = false;
	performancePanel_.Reset();
	playStatePanel_.Reset();
	playInputWasActive_ = false;
	preferences_ = EditorPreferences{};
	canSavePreferences_ = preferences_.Load();
	exportConfig_ = preferences_.gameExport;
	recentScenes_.Clear();

	for (auto it = preferences_.recentScenes.rbegin();
	     it != preferences_.recentScenes.rend(); ++it) {
		recentScenes_.Add(*it);
	}

	showGrid_ = preferences_.showGrid;
	sceneGrid_.SetSettings(preferences_.grid);
	showSelectionBounds_ = preferences_.showSelectionBounds;
	showCameraGuides_ = preferences_.showCameraGuides;
	showRespawnGuides_ = preferences_.showRespawnGuides;
	showObjectMarkers_ = preferences_.showObjectMarkers;
	colliderDisplay_ = preferences_.colliderDisplay;
	gizmo_.SetSnapSettings(preferences_.snap);
	gizmo_.SetSnapping(preferences_.snapping);
	gizmo_.SetOperation(preferences_.operation);
	gizmo_.SetSpace(preferences_.space);
	gizmo_.SetPivotMode(preferences_.pivotMode);

	cameraController_.Reset();
	cameraController_.SetSettings(preferences_.camera);

	stage_ = DialogStage::IDLE;
	pendingAction_ = EditorAction::NONE;
	pendingMaterialPath_.clear();
	exitQueued_ = false;
	closeApproved_ = false;
	errorOpenRequested_ = false;
	errorMessage_.clear();
	errorObjectId_ = 0;

	document_.New();
	sceneCheckPanel_.Reset();
	frameRequest_ = FrameRequest::NONE;
	frameObject_ = 0;
	selection_.Clear();
	ClearPlaySelection();

	hierarchyPanel_.Clear();
	inspectorPanel_.Clear();
	assetsPanel_.Clear();

	// 検索失敗時もEditorは起動し、Assetsパネルにエラーを表示する
	assets_.Refresh();
	clipboard_.Clear();

	Logger::Info("EditorSceneを初期化しました。");
	return true;
}

bool EditorScene::Update(float deltaTime) {
	if (!std::isfinite(deltaTime) || deltaTime < 0.0f) {
		Logger::Error("EditorScene::Update: 経過時間が無効です。");
		return false;
	}

	playFrame_ = false;
	playSceneReady_ = false;

	if (!playMode_.IsPlaying() && playSelectionActive_) {
		ClearPlaySelection();
	}

	performancePanel_.Record(deltaTime);
	gizmo_.BeginFrame();

	EditorAction action = DrawMenuBar();
	dockLayout_.Draw(ui_, resetDockLayout_);
	resetDockLayout_ = false;

	if (exitQueued_ && playMode_.IsPlaying()) {
		playMode_.Stop();
	}

	auto playAction = playMode_.DrawUI(ui_, !IsBusy(), exportConfig_,
	                                   !IsBusy() && playSelectionActive_ &&
	                                       playSelection_.GetCount() != 0);
	const bool playShortcutEnabled =
	    input_.IsActive() && playInputWasActive_ && !exitQueued_ &&
	    action == EditorAction::NONE && !cameraSettingsRequested_ &&
	    !gridSettingsRequested_ && stage_ == DialogStage::IDLE &&
	    !ui_.IsTextInputActive() && !ImGui::IsAnyItemActive() &&
	    !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
	                                     ImGuiPopupFlags_AnyPopupLevel) &&
	    !gizmo_.IsUsing() && !gizmoEditActive_ &&
	    !sceneViewPanel_.IsSelecting() && !document_.GetHistory().IsEditing() &&
	    !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
	    !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
	    !ImGui::IsMouseDown(ImGuiMouseButton_Middle);

	playInputWasActive_ = input_.IsActive();

	if (playAction == EditorPlayMode::Action::NONE) {
		playAction =
		    PlayShortcuts::Read(input_.GetKeyboard(), playMode_.IsPlaying(),
		                        playMode_.IsPaused(), playShortcutEnabled);
	}

	if (playAction == EditorPlayMode::Action::STOP_AND_KEEP_TRANSFORMS &&
	    !IsBusy()) {
		StopAndKeepSelectedTransforms();
	}

	if (playAction == EditorPlayMode::Action::STOP) {
		playMode_.Stop();
	}

	if (playAction == EditorPlayMode::Action::RENDER_ERROR) {
		playMode_.Stop();
		ShowError("Could not create or resize the Game render target.");
	}

	if (playAction == EditorPlayMode::Action::PLAY && !IsBusy()) {
		frameRequest_ = FrameRequest::NONE;
		frameObject_ = 0;
		FinishEdit();
		sceneViewPanel_.CancelSelection();
		std::string error;

		SceneObjectId invalidObject = 0;

		if (!playMode_.Start(document_.GetScene(), error,
		                     cameraController_.GetPose(), exportConfig_,
		                     &invalidObject)) {
			ShowError(std::move(error), invalidObject);
		}
	}

	if (playAction == EditorPlayMode::Action::RESTART &&
	    playMode_.IsPlaying()) {
		std::string error;

		if (!playMode_.Restart(error)) {
			ShowError(std::move(error));
		}
	}

	if (playAction == EditorPlayMode::Action::TOGGLE_PAUSE) {
		playMode_.TogglePause();
	} else if (playAction == EditorPlayMode::Action::STEP) {
		playMode_.RequestStep();
	}

	if (!playMode_.IsPlaying() && playSelectionActive_) {
		ClearPlaySelection();
	}

	if (playMode_.IsPlaying() && !playSelectionActive_) {
		try {
			const auto ids = selection_.GetIds();
			playSelection_.Set(
			    std::vector<SceneObjectId>(ids.begin(), ids.end()));
			playHierarchyPanel_.Clear();
			playSelectionActive_ = true;
		} catch (const std::exception &error) {
			Logger::Error(
			    std::format("EditorScene::PlaySelection: {}", error.what()));
			playMode_.Stop();
			ShowError("Could not prepare runtime selection. Your editing scene "
			          "is unchanged.");
		}
	}

	if (playMode_.IsPlaying()) {
		playFrame_ = true;
		DrawGridSettings();

		consolePanel_.Draw(ui_);
		controlsPanel_.Draw();
		const auto &runtime = playMode_.GetSession().GetScene();
		const auto hierarchy =
		    playHierarchyPanel_.Draw(ui_, runtime, playSelection_, false);

		if (hierarchy.action == HierarchyPanel::Action::FRAME_SELECTED) {
			action = EditorAction::FRAME_SELECTED;
		} else if (hierarchy.action ==
		           HierarchyPanel::Action::SELECT_FOLLOW_CAMERAS) {
			try {
				FollowCameraUsage::SelectObjects(runtime, playSelection_);
				playHierarchyPanel_.RequestReveal(playSelection_.GetActiveId());
			} catch (const std::exception &error) {
				Logger::Error(std::format(
				    "EditorScene::SelectRuntimeFollowers: {}", error.what()));
				ShowError("Could not select runtime Follow Cameras.");
			}
		}

		ui_.BeginDisabled(true);
		assetsPanel_.Draw(ui_, assets_);
		historyPanel_.Draw(ui_, document_.GetHistory(), false);
		ui_.EndDisabled();

		if (!sceneViewPanel_.DrawUI(ui_, gizmo_,
		                            TransformGizmo::SelectionMode::SINGLE,
		                            false, true)) {
			playMode_.Stop();
			ShowError("Could not create or resize the Scene render target.");
		} else if (!playMode_.Update(deltaTime, input_,
		                             sceneViewPanel_.GetInput().windowFocused ||
		                                 hierarchy.windowFocused,
		                             sceneViewPanel_.GetRenderTexture() !=
		                                 nullptr)) {
			playMode_.Stop();
			ShowError("Play stopped because simulation or Game rendering "
			          "failed. Your editing scene is unchanged.");
		}

		if (playMode_.IsPlaying() &&
		    !UpdatePlaySceneView(deltaTime, action, hierarchy.windowFocused)) {
			playMode_.Stop();
			ShowError("Play stopped because Scene rendering failed. Your "
			          "editing scene is unchanged.");
		}

		if (const auto copied = inspectorPanel_.DrawReadOnly(
		        ui_, playMode_.GetSession().GetScene(),
		        playSelection_.GetActiveId());
		    copied && !clipboard_.CopyTransform(*copied)) {
			ShowError("Could not copy an invalid runtime Transform.");
		}

		playStatePanel_.Draw(ui_, playMode_.GetSession());

		UpdateDialogs();

		if (!playMode_.IsPlaying()) {
			ClearPlaySelection();
		}

		return true;
	}

	if (exitQueued_ && stage_ == DialogStage::IDLE) {
		exitQueued_ = false;
		StartAction(EditorAction::EXIT);
	}

	DrawCameraSettings();
	DrawGridSettings();

	controlsPanel_.Draw();
	consolePanel_.Draw(ui_);
	playStatePanel_.Draw(ui_, playMode_.GetSession());

	const bool wasBusy = IsBusy();

	Scene &scene = document_.GetScene();
	EditorHistory &history = document_.GetHistory();

	ui_.BeginDisabled(wasBusy);

	const auto checkedObject =
	    sceneCheckPanel_.Draw(ui_, scene, cameraController_.GetPose());

	if (!wasBusy && checkedObject != 0) {
		FinishEdit();
		selection_.SelectOnly(checkedObject);
		hierarchyPanel_.RequestReveal(checkedObject);
	}

	const auto hierarchyRequest = hierarchyPanel_.Draw(
	    ui_, scene, selection_, true, clipboard_.GetCollider() != nullptr,
	    clipboard_.GetCameraLens() != nullptr);

	if (hierarchyRequest.selectionChanged) {
		FinishEdit();
	}

	// ギズモ操作とInspectorの編集を、同じ履歴として混在させない
	ui_.BeginDisabled(gizmo_.IsUsing() || gizmoEditActive_ ||
	                  sceneViewPanel_.IsSelecting());
	inspectorPanel_.Draw(ui_, scene, selection_.GetActiveId(), history,
	                     clipboard_.GetTransform() != nullptr, &models_,
	                     &assets_, clipboard_.GetCollider() != nullptr,
	                     clipboard_.GetCameraLens() != nullptr);
	const auto &objectRequest = inspectorPanel_.GetObjectRequest();
	const bool hasObjectRequest =
	    objectRequest.action != InspectorPanel::ObjectAction::NONE;
	const auto &modelRequest = inspectorPanel_.GetModelRequest();
	const auto &visibilityRequest = inspectorPanel_.GetVisibilityRequest();
	const auto colliderFitRequest = inspectorPanel_.GetColliderFitRequest();
	const auto &materialEditPath = inspectorPanel_.GetMaterialEditRequest();
	ui_.EndDisabled();

	if (!wasBusy && !inspectorPanel_.GetAssetRevealPath().empty()) {
		assetsPanel_.RequestReveal(inspectorPanel_.GetAssetRevealPath());
	}

	const auto requestedModelPath = assetsPanel_.Draw(ui_, assets_);
	const auto requestedInstances = assetsPanel_.GetInstanceSelectionRequest();
	const bool canAssignMaterial =
	    HasSelection() && !gizmo_.IsUsing() && !gizmoEditActive_ &&
	    !sceneViewPanel_.IsSelecting() &&
	    std::ranges::all_of(selection_.GetIds(), [&](const auto id) {
		    const auto *object = scene.FindObject(id);
		    return object && object->meshRenderer.has_value();
	    });
	materialPanel_.Draw(ui_, assets_, canAssignMaterial);
	const auto &materialAssignment = materialPanel_.GetAssignmentRequest();
	const auto &requestedMaterialUsers =
	    materialPanel_.GetUsageSelectionRequest();
	const auto historyRequest =
	    historyPanel_.Draw(ui_, history,
	                       !wasBusy && !gizmo_.IsUsing() && !gizmoEditActive_ &&
	                           !sceneViewPanel_.IsSelecting());

	const auto toolbarMode = selection_.GetCount() > 1
	                             ? TransformGizmo::SelectionMode::MULTIPLE
	                             : TransformGizmo::SelectionMode::SINGLE;
	const bool sceneViewReady =
	    sceneViewPanel_.DrawUI(ui_, gizmo_, toolbarMode);
	const auto &modelDrop = sceneViewPanel_.GetModelDrop();

	if (hierarchyRequest.windowFocused) {
		hierarchySelectionScope_ = true;
	} else if (sceneViewPanel_.GetInput().windowFocused) {
		hierarchySelectionScope_ = false;
	}

	ui_.EndDisabled();

	if (!sceneViewReady) {
		return false;
	}

	const auto *performanceTarget = sceneViewPanel_.GetRenderTexture();
	performancePanel_.Draw(
	    ui_, scene, selection_,
	    performanceTarget ? performanceTarget->GetWidth() : 0,
	    performanceTarget ? performanceTarget->GetHeight() : 0);

	if (!gizmoEditActive_ && !ImGui::IsAnyItemActive()) {
		history.CommitEdit();
	}

	if (!wasBusy) {
		if (hierarchyRequest.action != HierarchyPanel::Action::NONE) {
			// 編集中の内容を、物体を消す前に確定する
			FinishEdit();

			try {
				switch (hierarchyRequest.action) {
				case HierarchyPanel::Action::CREATE_EMPTY:
					CreateObject(SceneObject{});
					break;
				case HierarchyPanel::Action::CREATE_BOX:
					CreateObject(PrimitiveObjects::CreateBox());
					break;
				case HierarchyPanel::Action::CREATE_CAMERA:
					CreateCameraFromView();
					break;
				case HierarchyPanel::Action::DELETE_OBJECTS:
					DeleteSelectedObjects();
					break;
				case HierarchyPanel::Action::COPY_OBJECTS:
					CopySelectedObjects(false);
					break;
				case HierarchyPanel::Action::CUT_OBJECTS:
					CopySelectedObjects(true);
					break;
				case HierarchyPanel::Action::PASTE_COLLIDERS:
					PasteCollidersToSelection();
					break;
				case HierarchyPanel::Action::SNAP_POSITIONS:
					SnapSelectedPositions();
					break;
				case HierarchyPanel::Action::PASTE_CAMERA_LENSES:
					PasteCameraLensesToSelection();
					break;
				case HierarchyPanel::Action::DUPLICATE_ARRAY:
					StartObjectArray();
					break;
				case HierarchyPanel::Action::DUPLICATE_OBJECTS:
					DuplicateSelectedObjects();
					break;
				case HierarchyPanel::Action::RESET_TRANSFORMS:
					ResetSelectedTransforms();
					break;
				case HierarchyPanel::Action::DROP_TO_GROUND:
					DropSelectionToGround();
					break;
				case HierarchyPanel::Action::DROP_TO_SURFACE:
					DropSelectionToSurface();
					break;
				case HierarchyPanel::Action::ALIGN_X:
					AlignSelection(AlignmentAxis::X);
					break;
				case HierarchyPanel::Action::ALIGN_Y:
					AlignSelection(AlignmentAxis::Y);
					break;
				case HierarchyPanel::Action::ALIGN_Z:
					AlignSelection(AlignmentAxis::Z);
					break;
				case HierarchyPanel::Action::DISTRIBUTE_X:
					DistributeSelection(AlignmentAxis::X);
					break;
				case HierarchyPanel::Action::DISTRIBUTE_Y:
					DistributeSelection(AlignmentAxis::Y);
					break;
				case HierarchyPanel::Action::DISTRIBUTE_Z:
					DistributeSelection(AlignmentAxis::Z);
					break;
				case HierarchyPanel::Action::RENAME_OBJECT:
					if (HasSingleSelection()) {
						inspectorPanel_.RequestRename(selection_.GetActiveId());
					} else {
						StartBatchRename();
					}

					break;
				case HierarchyPanel::Action::FRAME_SELECTED:
					frameRequest_ = FrameRequest::SELECTED;
					break;
				case HierarchyPanel::Action::SELECT_FOLLOW_CAMERAS:
					SelectFollowingCameras();
					break;
				case HierarchyPanel::Action::HIDE_OBJECTS:
					ChangeVisibility(selection_.GetIds(), false);
					break;
				case HierarchyPanel::Action::SHOW_OBJECTS:
					ChangeVisibility(selection_.GetIds(), true);
					break;
				case HierarchyPanel::Action::NONE:
					break;
				}
			} catch (const std::exception &error) {
				Logger::Error(std::format(
				    "EditorScene: 物体の編集に失敗しました: {}", error.what()));

				ShowError(
				    "Could not edit the object. See the log for details.");
			}
		}

		if (!IsBusy() && action == EditorAction::NONE &&
		    colliderFitRequest != 0 &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			FitCollider(colliderFitRequest);
		}

		if (!IsBusy() && action == EditorAction::NONE &&
		    !requestedInstances.empty() && requestedModelPath.empty() &&
		    modelDrop.path.empty() && modelRequest.objectId == 0 &&
		    visibilityRequest.objectId == 0 && !hasObjectRequest &&
		    !historyRequest &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			SelectModelInstances(requestedInstances);
		}

		if (!IsBusy() && action == EditorAction::NONE && materialAssignment &&
		    checkedObject == 0 && requestedMaterialUsers.empty() &&
		    requestedInstances.empty() && requestedModelPath.empty() &&
		    modelDrop.path.empty() && modelRequest.objectId == 0 &&
		    visibilityRequest.objectId == 0 && colliderFitRequest == 0 &&
		    !hasObjectRequest && materialEditPath.empty() && !historyRequest &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			AssignMaterialToSelection(*materialAssignment);
		}

		if (!IsBusy() && action == EditorAction::NONE &&
		    !requestedMaterialUsers.empty() && requestedInstances.empty() &&
		    requestedModelPath.empty() && modelDrop.path.empty() &&
		    modelRequest.objectId == 0 && visibilityRequest.objectId == 0 &&
		    colliderFitRequest == 0 && !hasObjectRequest &&
		    materialEditPath.empty() && !historyRequest &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			SelectMaterialUsers(requestedMaterialUsers);
		}

		if (!IsBusy() && !requestedModelPath.empty()) {
			PlaceModel(requestedModelPath);
		}

		if (!IsBusy() && action == EditorAction::NONE &&
		    modelRequest.objectId != 0 &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			AssignModel(modelRequest.objectId, modelRequest.path);
		}

		if (!IsBusy() && action == EditorAction::NONE &&
		    visibilityRequest.objectId != 0 &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			ChangeVisibility({&visibilityRequest.objectId, 1},
			                 visibilityRequest.visible);
		}

		if (!IsBusy() && action == EditorAction::NONE && hasObjectRequest &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			ApplyInspectorRequest(objectRequest);
		}

		if (!IsBusy() && action == EditorAction::NONE &&
		    !materialEditPath.empty() && !hasObjectRequest &&
		    modelRequest.objectId == 0 && visibilityRequest.objectId == 0 &&
		    colliderFitRequest == 0 &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			OpenMaterialForEdit(materialEditPath);
		}

		if (!IsBusy()) {
			if (historyRequest && action == EditorAction::NONE &&
			    colliderFitRequest == 0 && requestedModelPath.empty() &&
			    requestedInstances.empty() && modelDrop.path.empty() &&
			    modelRequest.objectId == 0 && visibilityRequest.objectId == 0 &&
			    !hasObjectRequest && !materialAssignment &&
			    checkedObject == 0 && requestedMaterialUsers.empty() &&
			    materialEditPath.empty() &&
			    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
				RestoreHistory(*historyRequest);
			}

			if (action == EditorAction::NONE && colliderFitRequest == 0 &&
			    requestedInstances.empty() && modelDrop.path.empty() &&
			    modelRequest.objectId == 0 && visibilityRequest.objectId == 0 &&
			    !hasObjectRequest && !materialAssignment &&
			    checkedObject == 0 && requestedMaterialUsers.empty() &&
			    materialEditPath.empty() && !historyRequest) {
				const bool objectPanelFocused =
				    hierarchyRequest.windowFocused ||
				    sceneViewPanel_.GetInput().windowFocused;

				action = ReadShortcut(objectPanelFocused);
			}

			if (action != EditorAction::NONE) {
				StartAction(action);
			}
		}
	}

	UpdateDialogs();

	// 消えた物体だけを除き、残っている選択は維持する
	if (selection_.RemoveMissing(scene)) {
		FinishEdit();
	}

	const bool inputBlocked =
	    wasBusy || IsBusy() || action != EditorAction::NONE ||
	    checkedObject != 0 || materialAssignment.has_value() ||
	    !requestedModelPath.empty() || !requestedInstances.empty() ||
	    !requestedMaterialUsers.empty() || !modelDrop.path.empty() ||
	    modelRequest.objectId != 0 || visibilityRequest.objectId != 0 ||
	    colliderFitRequest != 0 || hasObjectRequest ||
	    historyRequest.has_value() ||
	    hierarchyRequest.action != HierarchyPanel::Action::NONE;

	ViewportInput viewport = sceneViewPanel_.GetInput();

	if (inputBlocked) {
		viewport = {};
	}

	ViewportInput cameraInput = viewport;

	if (gizmo_.IsUsing() || sceneViewPanel_.IsSelecting()) {
		cameraInput = {};
	}

	Transform orbitTransform;
	const DirectX::XMFLOAT3 *orbitTarget = nullptr;

	if (cameraInput.leftPressed && input_.GetKeyboard().IsDown(Key::ALT)) {
		if (selection_.GetCount() == 1) {
			if (const auto *object =
			        scene.FindObject(selection_.GetActiveId())) {
				orbitTransform = object->transform;
				orbitTarget = &orbitTransform.translation;
			}
		} else if (SelectionPivot::Make(scene, selection_,
		                                SelectionPivot::Mode::CENTER,
		                                orbitTransform)) {
			orbitTarget = &orbitTransform.translation;
		}
	}

	cameraController_.Update(input_, cameraInput, deltaTime, orbitTarget);

	auto *target = sceneViewPanel_.GetRenderTexture();

	if (!target) {
		sceneViewPanel_.CancelSelection();

		if (gizmoEditActive_) {
			history.CommitEdit();
			gizmoEditActive_ = false;
		}

		gizmo_.Cancel();
		return playMode_.UpdatePreview(scene, cameraController_.GetPose());
	}

	if (!renderer_.SetCameraPose(cameraController_.GetPose())) {
		Logger::Error("EditorScene: カメラの姿勢が無効です。");
		return false;
	}

	if (!renderer_.Update(target->GetWidth(), target->GetHeight())) {
		return false;
	}

	DirectX::XMFLOAT4X4 view;
	DirectX::XMFLOAT4X4 projection;

	if (!renderer_.GetCameraMatrices(view, projection)) {
		Logger::Error("EditorScene: カメラ行列を取得できませんでした。");
		return false;
	}

	FrameRequest frameRequest = frameRequest_;
	frameRequest_ = FrameRequest::NONE;

	if (!inputBlocked && frameRequest == FrameRequest::NONE) {
		frameRequest =
		    ReadFrameShortcut(viewport, hierarchyRequest.windowFocused);
	}

	if (frameRequest != FrameRequest::NONE &&
	    FrameObjects(frameRequest, projection)) {
		// 移動後のカメラを、ギズモや選択判定にも同じフレームで反映する
		if (!renderer_.SetCameraPose(cameraController_.GetPose())) {
			Logger::Error("EditorScene: 移動後のカメラの姿勢が無効です。");
			return false;
		}

		if (!renderer_.Update(target->GetWidth(), target->GetHeight())) {
			return false;
		}

		if (!renderer_.GetCameraMatrices(view, projection)) {
			Logger::Error(
			    "EditorScene: 移動後のカメラ行列を取得できませんでした。");
			return false;
		}
	}

	if (!wasBusy && !IsBusy() && action == EditorAction::NONE &&
	    hierarchyRequest.action == HierarchyPanel::Action::NONE &&
	    !modelDrop.path.empty()) {
		Ray ray;

		if (!renderer_.CreatePickingRay(modelDrop.imagePosition.x,
		                                modelDrop.imagePosition.y, ray)) {
			ShowError("Could not determine the model placement position.");
		} else if (PlaceDroppedModel(modelDrop.path, ray) &&
		           !renderer_.Update(target->GetWidth(), target->GetHeight())) {
			return false;
		}
	}

	const SceneObjectId activeId = selection_.GetActiveId();
	const bool multipleSelection = selection_.GetCount() > 1;
	const auto selectionMode = multipleSelection
	                               ? TransformGizmo::SelectionMode::MULTIPLE
	                               : TransformGizmo::SelectionMode::SINGLE;

	SceneObject *selected = scene.FindObject(activeId);

	Transform *transform = nullptr;

	if (multipleSelection) {
		if (gizmoEditActive_ ||
		    MakeSelectionTransform(selectionGizmoTransform_)) {
			transform = &selectionGizmoTransform_;
		}
	} else if (HasSingleSelection() && selected) {
		transform = &selected->transform;
	}

	const Transform beforeGizmo = transform ? *transform : Transform{};

	const bool gizmoEnabled =
	    !inputBlocked && transform != nullptr && viewport.applicationFocused &&
	    !viewport.rightDown && !viewport.middleDown && !ImGui::GetIO().KeyAlt &&
	    (!viewport.widgetActive || gizmo_.IsUsing());

	const auto gizmoResult =
	    sceneViewPanel_.DrawGizmo(ui_, gizmo_, view, projection, activeId,
	                              transform, gizmoEnabled, selectionMode);

	if (gizmoResult.changed) {
		if (!gizmoEditActive_) {
			if (multipleSelection) {
				// ギズモ用の値だけが変わった段階で、全物体の開始値を保持する
				if (!history.BeginTransforms(selection_.GetIds())) {
					Logger::Error("EditorScene: "
					              "複数物体の編集履歴を開始できませんでした。");
					return false;
				}

				selectionGizmoStart_ = beforeGizmo;
			} else if (!history.BeginTransform(activeId, beforeGizmo)) {
				Logger::Error(
				    "EditorScene: ギズモの編集履歴を開始できませんでした。");
				return false;
			}

			gizmoEditActive_ = true;
		}

		bool applied = true;

		if (multipleSelection) {
			applied = history.UpdateTransforms(selectionGizmoStart_,
			                                   selectionGizmoTransform_);

			if (!applied) {
				// 無効な変形は反映せず、直前の有効な状態で編集を確定する
				selectionGizmoTransform_ = beforeGizmo;
				FinishEdit();
				Logger::Error("EditorScene: 複数物体の変形情報が無効です。");
			}
		}

		if (applied &&
		    !renderer_.Update(target->GetWidth(), target->GetHeight())) {
			return false;
		}
	}

	if (gizmoEditActive_ && !gizmo_.IsUsing()) {
		history.CommitEdit();
		gizmoEditActive_ = false;
	}

	const auto selectionRequest = sceneViewPanel_.UpdateSelection(
	    ui_, !inputBlocked, gizmoResult.blocksSelection);

	if (selectionRequest.action !=
	    SceneViewPanel::SelectionRequest::Action::NONE) {
		try {
			if (ApplySceneSelection(selectionRequest, view, projection)) {
				FinishEdit();
			}
		} catch (const std::exception &error) {
			Logger::Error(std::format(
			    "EditorScene: 物体の選択に失敗しました: {}", error.what()));
			ShowError("Could not select objects. See the log for details.");
		}
	}

	// ギズモによる再更新とクリック選択が終わってから登録する
	if (showGrid_ && !sceneGrid_.Update(renderer_)) {
		Logger::Error("EditorScene: グリッドを登録できませんでした。");
		return false;
	}

	if (!colliderBounds_.Update(renderer_, scene, selection_.GetIds(),
	                            colliderDisplay_)) {
		Logger::Error("EditorScene: 衝突形状を表示できませんでした。");
		return false;
	}

	if (showObjectMarkers_ &&
	    !objectMarkers_.Update(renderer_, scene, selection_.GetIds())) {
		Logger::Error("EditorScene: 物体の目印を登録できませんでした。");
		return false;
	}

	for (const SceneObjectId id : selection_.GetIds()) {
		if (const auto *object = scene.FindObject(id);
		    showRespawnGuides_ && object &&
		    !respawnPreview_.Update(renderer_, scene, id)) {
			Logger::Error(
			    "EditorScene: 落下復帰のプレビューを登録できませんでした。");
			return false;
		}

		if (showCameraGuides_ &&
		    !cameraFrustum_.Update(renderer_, scene, id, exportConfig_.width,
		                           exportConfig_.height)) {
			Logger::Error(
			    "EditorScene: カメラの撮影範囲を登録できませんでした。");
			return false;
		}

		if (showCameraGuides_ &&
		    !followCameraPreview_.Update(renderer_, scene, id)) {
			Logger::Error(
			    "EditorScene: カメラの追従関係を登録できませんでした。");
			return false;
		}

		if (showSelectionBounds_ && !selectionBounds_.Update(renderer_, id)) {
			Logger::Error(
			    "EditorScene: 選択物体の外接ボックスを登録できませんでした。");
			return false;
		}
	}

	// 全ての線を登録した後、一度だけ頂点バッファへ転送する
	if (!renderer_.UpdateDebugLines()) {
		Logger::Error("EditorScene: 線の頂点バッファを更新できませんでした。");
		return false;
	}

	return playMode_.UpdatePreview(scene, cameraController_.GetPose());
}

void EditorScene::Draw() const {
	playMode_.Draw();

	if (playFrame_) {
		if (auto *target = sceneViewPanel_.GetRenderTexture();
		    playSceneReady_ && target) {
			playSceneRenderer_.Draw(target);
		}

		return;
	}

	auto *target = sceneViewPanel_.GetRenderTexture();

	if (target) {
		renderer_.Draw(target);
	}
}

void EditorScene::Finalize() {
	FinishEdit();

	if (canSavePreferences_) {
		EditorPreferences current;
		current.showGrid = showGrid_;
		current.showSelectionBounds = showSelectionBounds_;
		current.showCameraGuides = showCameraGuides_;
		current.showRespawnGuides = showRespawnGuides_;
		current.showObjectMarkers = showObjectMarkers_;
		current.colliderDisplay = colliderDisplay_;
		current.operation = gizmo_.GetOperation();
		current.space = gizmo_.GetSpace();
		current.pivotMode = gizmo_.GetPivotMode();
		current.snapping = gizmo_.IsSnapping();
		current.snap = gizmo_.GetSnapSettings();
		current.camera = cameraController_.GetSettings();
		current.grid = sceneGrid_.GetSettings();
		current.gameExport = exportConfig_;
		const auto recent = recentScenes_.GetPaths();
		current.recentScenes.assign(recent.begin(), recent.end());

		if (!(current == preferences_) && current.Save()) {
			preferences_ = current;
		}
	}

	performancePanel_.Reset();
	playStatePanel_.Reset();
	playSceneReady_ = false;
	ClearPlaySelection();
	playSceneRenderer_.Finalize();
	playMode_.Finalize();
	sceneViewPanel_.Finalize();
	renderer_.Finalize();

	document_.New();
	sceneCheckPanel_.Reset();
	frameRequest_ = FrameRequest::NONE;
	frameObject_ = 0;
	selection_.Clear();

	hierarchyPanel_.Clear();
	inspectorPanel_.Clear();

	stage_ = DialogStage::IDLE;
	pendingAction_ = EditorAction::NONE;
	pendingMaterialPath_.clear();
	exitQueued_ = false;
	closeApproved_ = false;
	errorOpenRequested_ = false;
	errorMessage_.clear();
	errorObjectId_ = 0;

	assetsPanel_.Clear();
	assets_.Clear();
	clipboard_.Clear();

	cameraController_.Reset();
}

bool EditorScene::RequestClose() {
	// ほかのダイアログ中の終了要求は、操作完了まで保持する
	if (!closeApproved_ && pendingAction_ != EditorAction::EXIT) {
		exitQueued_ = true;
	}

	return false;
}

bool EditorScene::IsCloseApproved() const {
	return closeApproved_;
}

std::string EditorScene::GetWindowTitle() const {
	const std::string name =
	    document_.GetFilePath().empty()
	        ? "Untitled"
	        : PathUtils::ToUtf8(document_.GetFilePath().filename());

	return std::format(
	    "{}{} - Editor", name,
	    document_.IsModified() || materialPanel_.HasUnsavedChanges() ? "*"
	                                                                 : "");
}

EditorScene::EditorAction EditorScene::DrawMenuBar() {
	EditorAction action = EditorAction::NONE;

	if (!ui_.BeginMainMenuBar()) {
		return action;
	}

	ui_.BeginDisabled(playMode_.IsPlaying());

	if (ui_.BeginMenu("File")) {
		const bool enabled = !IsBusy();

		if (ui_.MenuItem("New", "Ctrl+N", enabled)) {
			action = EditorAction::NEW;
		}

		if (ui_.MenuItem("New Sample Stage...", nullptr, enabled)) {
			action = EditorAction::NEW_SAMPLE;
		}

		if (ui_.MenuItem("Open...", "Ctrl+O", enabled)) {
			action = EditorAction::OPEN;
		}

		ui_.BeginDisabled(!enabled || recentScenes_.GetPaths().empty());

		if (ui_.BeginMenu("Open Recent")) {
			std::size_t index = 0;

			for (const auto &path : recentScenes_.GetPaths()) {
				const auto label =
				    std::format("{}###RecentScene{}",
				                PathUtils::ToUtf8(path.filename()), index++);

				if (ui_.MenuItem(label.c_str(), nullptr, enabled)) {
					// 保存確認中に一覧が更新されても、開くパスは変えない
					recentOpenPath_ = path;
					action = EditorAction::OPEN_RECENT;
				}

				ImGui::SetItemTooltip("%s", PathUtils::ToUtf8(path).c_str());
			}

			ui_.Separator();

			if (ui_.MenuItem("Clear Recent", nullptr, enabled)) {
				recentScenes_.Clear();
			}

			ui_.EndMenu();
		}

		ui_.EndDisabled();

		ui_.Separator();

		if (ui_.MenuItem("Save", "Ctrl+S", enabled)) {
			action = EditorAction::SAVE;
		}

		if (ui_.MenuItem("Save As...", "Ctrl+Shift+S", enabled)) {
			action = EditorAction::SAVE_AS;
		}

		if (ui_.MenuItem("Save Copy...", nullptr, enabled)) {
			action = EditorAction::SAVE_COPY;
		}

		if (ui_.MenuItem("Game Settings...", nullptr, enabled)) {
			action = EditorAction::GAME_SETTINGS;
		}

		if (ui_.MenuItem("Export Game...", nullptr, enabled)) {
			action = EditorAction::EXPORT_GAME;
		}

		if (ui_.MenuItem("Reload Scene...", nullptr,
		                 enabled && !document_.GetFilePath().empty())) {
			action = EditorAction::RELOAD_SCENE;
		}

		ui_.Separator();

		if (ui_.MenuItem("Exit", nullptr, enabled)) {
			action = EditorAction::EXIT;
		}

		ui_.EndMenu();
	}

	if (ui_.BeginMenu("Edit")) {
		const EditorHistory &history = document_.GetHistory();

		const bool enabled = !IsBusy() && !history.IsEditing() &&
		                     !gizmo_.IsUsing() &&
		                     !sceneViewPanel_.IsSelecting();

		const auto undo = history.GetUndoDescription();
		const auto redo = history.GetRedoDescription();
		const auto undoLabel = undo.empty()
		                           ? std::string("Undo###Undo")
		                           : std::format("Undo {}###Undo", undo);
		const auto redoLabel = redo.empty()
		                           ? std::string("Redo###Redo")
		                           : std::format("Redo {}###Redo", redo);

		if (ui_.MenuItem(undoLabel.c_str(), "Ctrl+Z",
		                 enabled && history.CanUndo())) {
			action = EditorAction::UNDO;
		}

		if (ui_.MenuItem(redoLabel.c_str(), "Ctrl+Y",
		                 enabled && history.CanRedo())) {
			action = EditorAction::REDO;
		}

		ui_.Separator();

		const bool objectEnabled =
		    enabled && !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
		    !ImGui::IsMouseDown(ImGuiMouseButton_Middle) && HasSelection();

		if (ui_.MenuItem("Select All", "Ctrl+A", enabled)) {
			action = EditorAction::SELECT_ALL;
		}

		if (ui_.MenuItem("Deselect All", "Esc", enabled && HasSelection())) {
			action = EditorAction::DESELECT_ALL;
		}

		if (ui_.MenuItem("Reveal Active in Hierarchy", nullptr,
		                 objectEnabled)) {
			action = EditorAction::REVEAL_ACTIVE;
		}

		const auto &objects = document_.GetScene().GetObjects();
		const bool hasHidden =
		    std::any_of(objects.begin(), objects.end(), [](const auto &entry) {
			    return entry.second.IsHidden();
		    });

		if (ui_.MenuItem("Show All Models", nullptr, enabled && hasHidden)) {
			action = EditorAction::SHOW_ALL_MODELS;
		}

		ui_.Separator();

		if (ui_.MenuItem("Copy", "Ctrl+C", objectEnabled)) {
			action = EditorAction::COPY_OBJECTS;
		}

		if (ui_.MenuItem("Cut", "Ctrl+X", objectEnabled)) {
			action = EditorAction::CUT_OBJECTS;
		}

		if (ui_.MenuItem("Paste", "Ctrl+V",
		                 enabled && clipboard_.HasObjects())) {
			action = EditorAction::PASTE_OBJECTS;
		}

		ui_.Separator();

		if (ui_.MenuItem("Duplicate", "Ctrl+D", objectEnabled)) {
			action = EditorAction::DUPLICATE_OBJECTS;
		}

		if (ui_.MenuItem("Duplicate with Offset...", nullptr, objectEnabled)) {
			action = EditorAction::DUPLICATE_ARRAY;
		}

		if (ui_.MenuItem("Delete", "Delete", objectEnabled)) {
			action = EditorAction::DELETE_OBJECTS;
		}

		if (ui_.MenuItem("Reset Transform", nullptr, objectEnabled)) {
			action = EditorAction::RESET_TRANSFORMS;
		}

		if (ui_.MenuItem("Snap Positions to Grid", nullptr, objectEnabled)) {
			action = EditorAction::SNAP_POSITIONS;
		}

		if (ui_.MenuItem("Drop to Ground", "Shift+G",
		                 objectEnabled && HasVisibleSelectedModel())) {
			action = EditorAction::DROP_TO_GROUND;
		}

		if (ui_.MenuItem("Drop Colliders to Surface", nullptr,
		                 objectEnabled &&
		                     ColliderPlacement::CanDrop(document_.GetScene(),
		                                                selection_.GetIds()))) {
			action = EditorAction::DROP_TO_SURFACE;
		}

		if (ui_.MenuItem("Paste Camera Lenses", nullptr,
		                 objectEnabled &&
		                     clipboard_.GetCameraLens() != nullptr)) {
			action = EditorAction::PASTE_CAMERA_LENSES;
		}

		if (ui_.BeginMenu("Box Colliders")) {
			bool hasCollider = false;
			bool hasMissing = false;

			for (const auto id : selection_.GetIds()) {
				if (const auto *object = document_.GetScene().FindObject(id)) {
					hasCollider |= object->collider.has_value();
					hasMissing |= !object->collider.has_value();
				}
			}

			if (ui_.MenuItem("Add Missing", nullptr,
			                 objectEnabled && hasMissing)) {
				action = EditorAction::ADD_COLLIDERS;
			}

			if (ui_.MenuItem("Fit All to Models", nullptr, objectEnabled)) {
				action = EditorAction::FIT_COLLIDERS;
			}

			if (ui_.MenuItem("Paste Box Colliders", nullptr,
			                 objectEnabled &&
			                     clipboard_.GetCollider() != nullptr)) {
				action = EditorAction::PASTE_COLLIDERS;
			}

			if (ui_.MenuItem("Remove All", nullptr,
			                 objectEnabled && hasCollider)) {
				action = EditorAction::REMOVE_COLLIDERS;
			}

			ui_.Separator();
			ui_.BeginDisabled(!objectEnabled || selection_.GetCount() < 2 ||
			                  !ColliderPlacement::CanDrop(document_.GetScene(),
			                                              selection_.GetIds()));

			if (ui_.BeginMenu("Place Next to Active")) {
				if (ui_.MenuItem("Right (+X)", nullptr, true)) {
					action = EditorAction::PLACE_COLLIDERS_RIGHT;
				}

				if (ui_.MenuItem("Left (-X)", nullptr, true)) {
					action = EditorAction::PLACE_COLLIDERS_LEFT;
				}

				if (ui_.MenuItem("Above (+Y)", nullptr, true)) {
					action = EditorAction::PLACE_COLLIDERS_ABOVE;
				}

				if (ui_.MenuItem("Below (-Y)", nullptr, true)) {
					action = EditorAction::PLACE_COLLIDERS_BELOW;
				}

				if (ui_.MenuItem("Front (+Z)", nullptr, true)) {
					action = EditorAction::PLACE_COLLIDERS_FRONT;
				}

				if (ui_.MenuItem("Behind (-Z)", nullptr, true)) {
					action = EditorAction::PLACE_COLLIDERS_BACK;
				}

				ui_.EndMenu();
			}

			ui_.EndDisabled();

			ui_.EndMenu();
		}

		if (ui_.MenuItem("Align X to Active", nullptr,
		                 objectEnabled && selection_.GetCount() > 1)) {
			action = EditorAction::ALIGN_X;
		}

		if (ui_.MenuItem("Align Y to Active", nullptr,
		                 objectEnabled && selection_.GetCount() > 1)) {
			action = EditorAction::ALIGN_Y;
		}

		if (ui_.MenuItem("Align Z to Active", nullptr,
		                 objectEnabled && selection_.GetCount() > 1)) {
			action = EditorAction::ALIGN_Z;
		}

		if (ui_.MenuItem("Distribute X", nullptr,
		                 objectEnabled && selection_.GetCount() > 2)) {
			action = EditorAction::DISTRIBUTE_X;
		}

		if (ui_.MenuItem("Distribute Y", nullptr,
		                 objectEnabled && selection_.GetCount() > 2)) {
			action = EditorAction::DISTRIBUTE_Y;
		}

		if (ui_.MenuItem("Distribute Z", nullptr,
		                 objectEnabled && selection_.GetCount() > 2)) {
			action = EditorAction::DISTRIBUTE_Z;
		}

		if (ui_.MenuItem("Rename", "F2", objectEnabled)) {
			action = EditorAction::RENAME_OBJECT;
		}

		ui_.EndMenu();
	}

	if (ui_.BeginMenu("Create")) {
		const bool enabled = !IsBusy() && !document_.GetHistory().IsEditing() &&
		                     !gizmo_.IsUsing() &&
		                     !sceneViewPanel_.IsSelecting();

		if (ui_.MenuItem("Empty", nullptr, enabled)) {
			action = EditorAction::CREATE_EMPTY;
		}

		if (ui_.MenuItem("Box", nullptr, enabled)) {
			action = EditorAction::CREATE_BOX;
		}

		if (ui_.MenuItem("Camera from Scene View", nullptr, enabled)) {
			action = EditorAction::CREATE_CAMERA;
		}

		ui_.EndMenu();
	}

	ui_.EndDisabled();

	if (ui_.BeginMenu("View")) {
		const bool enabled = !IsBusy() && !document_.GetHistory().IsEditing() &&
		                     !gizmo_.IsUsing() &&
		                     !sceneViewPanel_.IsSelecting();

		if (ui_.MenuItem("Frame Selected", "F",
		                 enabled && (playMode_.IsPlaying()
		                                 ? playSelection_.GetCount() > 0
		                                 : HasSelection()))) {
			action = EditorAction::FRAME_SELECTED;
		}

		if (ui_.MenuItem("Frame All", "Shift+F", enabled)) {
			action = EditorAction::FRAME_ALL;
		}

		ui_.BeginDisabled(playMode_.IsPlaying());

		if (ui_.BeginMenu("Camera View")) {
			if (ui_.MenuItem("Front (-Z)", nullptr, enabled)) {
				action = EditorAction::VIEW_FRONT;
			}

			if (ui_.MenuItem("Back (+Z)", nullptr, enabled)) {
				action = EditorAction::VIEW_BACK;
			}

			if (ui_.MenuItem("Left (-X)", nullptr, enabled)) {
				action = EditorAction::VIEW_LEFT;
			}

			if (ui_.MenuItem("Right (+X)", nullptr, enabled)) {
				action = EditorAction::VIEW_RIGHT;
			}

			if (ui_.MenuItem("Top (+Y)", nullptr, enabled)) {
				action = EditorAction::VIEW_TOP;
			}

			if (ui_.MenuItem("Bottom (-Y)", nullptr, enabled)) {
				action = EditorAction::VIEW_BOTTOM;
			}

			ui_.EndMenu();
		}

		if (ui_.MenuItem("Reset Camera", nullptr, enabled)) {
			action = EditorAction::RESET_CAMERA;
		}

		if (ui_.MenuItem("Camera Settings...", nullptr, enabled)) {
			cameraSettingsRequested_ = true;
		}

		ui_.EndDisabled();

		ui_.Separator();
		ImGui::MenuItem("Grid", nullptr, &showGrid_, !IsBusy());

		if (ui_.MenuItem("Grid Settings...", nullptr, !IsBusy())) {
			gridSettingsRequested_ = true;
		}

		ImGui::MenuItem("Selection Bounds", nullptr, &showSelectionBounds_,
		                !IsBusy());
		ImGui::MenuItem("Camera Guides", nullptr, &showCameraGuides_,
		                !IsBusy());
		ImGui::MenuItem("Respawn Guides", nullptr, &showRespawnGuides_,
		                !IsBusy());
		ImGui::MenuItem("Object Markers", nullptr, &showObjectMarkers_,
		                !IsBusy());

		if (ui_.BeginMenu("Colliders")) {
			const auto item = [&](const char *label,
			                      ColliderBounds::Mode mode) {
				if (ImGui::MenuItem(label, nullptr, colliderDisplay_ == mode,
				                    !IsBusy())) {
					colliderDisplay_ = mode;
				}
			};
			item("None", ColliderBounds::Mode::NONE);
			item("Selected", ColliderBounds::Mode::SELECTED);
			item("All", ColliderBounds::Mode::ALL);
			ui_.EndMenu();
		}

		ui_.Separator();

		if (ui_.MenuItem("Reset Layout", nullptr, !IsBusy())) {
			resetDockLayout_ = true;
		}

		if (ui_.MenuItem("History...", nullptr, !IsBusy())) {
			historyPanel_.Show();
		}

		if (ui_.MenuItem("Console...", nullptr, !IsBusy())) {
			consolePanel_.Show();
		}

		if (ui_.MenuItem("Scene Check...", nullptr,
		                 !IsBusy() && !playMode_.IsPlaying())) {
			sceneCheckPanel_.Show();
		}

		if (ui_.MenuItem("Performance...", nullptr, !IsBusy())) {
			performancePanel_.Show();
		}

		if (ui_.MenuItem("Play State...", nullptr,
		                 stage_ == DialogStage::IDLE && !exitQueued_)) {
			playStatePanel_.Show();
		}

		ui_.EndMenu();
	}

	if (ui_.BeginMenu("Help")) {
		if (ui_.MenuItem("Controls...", nullptr, !IsBusy())) {
			controlsPanel_.Show();
		}

		ui_.EndMenu();
	}

	ui_.EndMainMenuBar();
	return action;
}

void EditorScene::DrawGridSettings() {
	if (gridSettingsRequested_) {
		ImGui::OpenPopup("EditorGridSettings");
		gridSettingsRequested_ = false;
	}

	if (!ImGui::BeginPopup("EditorGridSettings")) {
		return;
	}

	ImGui::BeginDisabled(IsBusy() || gizmo_.IsUsing() ||
	                     sceneViewPanel_.IsSelecting());
	ImGui::Checkbox("Show grid", &showGrid_);
	auto settings = sceneGrid_.GetSettings();
	bool changed = ImGui::DragFloat("Spacing", &settings.spacing, 0.01f, 0.001f,
	                                SceneGrid::kMaximumSpacing, "%.3f");
	ImGui::SetItemTooltip("Distance between grid lines.");
	changed |= ImGui::SliderInt("Cells per side", &settings.halfLineCount, 1,
	                            SceneGrid::kMaximumHalfLineCount);
	ImGui::SetItemTooltip("Number of cells from the origin to either edge.");
	changed |= ImGui::SliderInt("Major line every", &settings.majorLineInterval,
	                            1, SceneGrid::kMaximumMajorLineInterval);

	if (ImGui::Button("Restore Defaults")) {
		settings = SceneGrid::Settings{};
		changed = true;
	}

	if (changed) {
		sceneGrid_.SetSettings(settings);
	}

	ImGui::EndDisabled();
	ImGui::EndPopup();
}

void EditorScene::DrawCameraSettings() {
	if (cameraSettingsRequested_) {
		ImGui::OpenPopup("EditorCameraSettings");
		cameraSettingsRequested_ = false;
	}

	if (!ImGui::BeginPopup("EditorCameraSettings")) {
		return;
	}

	ImGui::BeginDisabled(gizmo_.IsUsing() || sceneViewPanel_.IsSelecting() ||
	                     ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
	                     ImGui::IsMouseDown(ImGuiMouseButton_Middle));
	auto settings = cameraController_.GetSettings();
	bool changed = ImGui::DragFloat("Move speed", &settings.moveSpeed, 0.1f,
	                                0.01f, 10000.0f, "%.2f");
	changed |= ImGui::DragFloat("Shift multiplier", &settings.fastMultiplier,
	                            0.1f, 1.0f, 100.0f, "%.2f");
	changed |= ImGui::DragFloat("Mouse sensitivity", &settings.mouseSensitivity,
	                            0.0001f, 0.00001f, 1.0f, "%.5f");
	changed |= ImGui::DragFloat("Wheel distance", &settings.wheelDistance,
	                            0.01f, 0.01f, 10000.0f);
	ImGui::SetItemTooltip("Distance per wheel step. Scroll over the Scene to "
	                      "move forward or backward.");
	changed |= ImGui::DragFloat("Pan sensitivity", &settings.panSensitivity,
	                            0.0001f, 0.00001f, 100.0f, "%.5f");
	ImGui::SetItemTooltip("Middle-drag the Scene to pan the camera.");

	if (ImGui::Button("Restore Defaults")) {
		settings = EditorCameraController::Settings{};
		changed = true;
	}

	if (changed) {
		cameraController_.SetSettings(settings);
	}

	ImGui::EndDisabled();
	ImGui::EndPopup();
}

EditorScene::EditorAction
EditorScene::ReadShortcut(bool objectPanelFocused) const {
	// 文字入力中の操作は、ImGuiの入力欄へ任せる
	if (IsBusy() || ui_.IsTextInputActive() || gizmo_.IsUsing() ||
	    sceneViewPanel_.IsSelecting() || document_.GetHistory().IsEditing()) {
		return EditorAction::NONE;
	}

	const Keyboard &keyboard = input_.GetKeyboard();

	const bool control = keyboard.IsDown(Key::CTRL);
	const bool shift = keyboard.IsDown(Key::SHIFT);
	const bool alt = keyboard.IsDown(Key::ALT);

	const bool selectionEnabled = objectPanelFocused &&
	                              !ImGui::IsAnyItemActive() &&
	                              !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
	                              !ImGui::IsMouseDown(ImGuiMouseButton_Middle);
	const bool objectEnabled = selectionEnabled && HasSelection();

	if (objectEnabled && !control && shift && !alt &&
	    keyboard.IsPressed(Key::G) && HasVisibleSelectedModel()) {
		return EditorAction::DROP_TO_GROUND;
	}

	if (objectEnabled && !control && !shift && !alt) {
		if (keyboard.IsPressed(Key::F2)) {
			return EditorAction::RENAME_OBJECT;
		}

		if (keyboard.IsPressed(Key::W)) {
			return EditorAction::GIZMO_MOVE;
		}

		if (keyboard.IsPressed(Key::E)) {
			return EditorAction::GIZMO_ROTATE;
		}

		if (keyboard.IsPressed(Key::R)) {
			return EditorAction::GIZMO_SCALE;
		}

		if (selection_.GetCount() > 1 && keyboard.IsPressed(Key::Z)) {
			return EditorAction::GIZMO_PIVOT;
		}

		if (HasSingleSelection() &&
		    gizmo_.GetOperation() != TransformGizmo::Operation::SCALE &&
		    keyboard.IsPressed(Key::X)) {
			return EditorAction::GIZMO_SPACE;
		}
	}

	if (selectionEnabled && !control && !shift && !alt &&
	    keyboard.IsPressed(Key::ESCAPE)) {
		return EditorAction::DESELECT_ALL;
	}

	if (selectionEnabled && control && !shift && !alt &&
	    keyboard.IsPressed(Key::A)) {
		return EditorAction::SELECT_ALL;
	}

	if (selectionEnabled && control && !shift && !alt) {
		if (objectEnabled && keyboard.IsPressed(Key::C)) {
			return EditorAction::COPY_OBJECTS;
		}

		if (objectEnabled && keyboard.IsPressed(Key::X)) {
			return EditorAction::CUT_OBJECTS;
		}

		if (clipboard_.HasObjects() && keyboard.IsPressed(Key::V)) {
			return EditorAction::PASTE_OBJECTS;
		}
	}

	if (objectEnabled && !control && !shift && !alt &&
	    keyboard.IsPressed(Key::DELETE_KEY)) {
		return EditorAction::DELETE_OBJECTS;
	}

	if (!control) {
		return EditorAction::NONE;
	}

	if (objectEnabled && !shift && !alt && keyboard.IsPressed(Key::D)) {
		return EditorAction::DUPLICATE_OBJECTS;
	}

	if (keyboard.IsPressed(Key::Z)) {
		return shift ? EditorAction::REDO : EditorAction::UNDO;
	}

	if (keyboard.IsPressed(Key::Y)) {
		return EditorAction::REDO;
	}

	if (keyboard.IsPressed(Key::S)) {
		return shift ? EditorAction::SAVE_AS : EditorAction::SAVE;
	}

	if (keyboard.IsPressed(Key::O)) {
		return EditorAction::OPEN;
	}

	if (keyboard.IsPressed(Key::N)) {
		return EditorAction::NEW;
	}

	return EditorAction::NONE;
}

void EditorScene::StartAction(EditorAction action) {
	if (stage_ != DialogStage::IDLE || closeApproved_ ||
	    action == EditorAction::NONE) {
		return;
	}

	// 保存やUndoを実行する前に、編集中の値を確定する
	FinishEdit();

	switch (action) {
	case EditorAction::GAME_SETTINGS:
		gameSettingsDialog_.Show(exportConfig_);
		stage_ = DialogStage::GAME_SETTINGS;
		return;
	case EditorAction::EXPORT_GAME: {
		pendingAction_ = action;

		try {
			if (materialPanel_.HasUnsavedChanges() &&
			    !MaterialUsage::FindObjects(document_.GetScene(),
			                                materialPanel_.GetFilePath())
			         .empty()) {
				saveChangesDialog_.Show(
				    PathUtils::ToUtf8(materialPanel_.GetFilePath().filename()));
				stage_ = DialogStage::CONFIRM_MATERIAL_SAVE;
			} else {
				ContinueAfterMaterialConfirmation();
			}
		} catch (const std::exception &error) {
			Logger::Error(
			    std::format("EditorScene::PrepareExport: {}", error.what()));
			ShowError("Could not prepare game export. Your edits were kept. "
			          "See the log for details.");
		}

		return;
	}

	case EditorAction::RENAME_OBJECT:
		if (HasSingleSelection()) {
			inspectorPanel_.RequestRename(selection_.GetActiveId());
		} else {
			StartBatchRename();
		}

		return;
	case EditorAction::VIEW_FRONT:
		SetCameraView(CameraView::FRONT);
		return;
	case EditorAction::VIEW_BACK:
		SetCameraView(CameraView::BACK);
		return;
	case EditorAction::VIEW_LEFT:
		SetCameraView(CameraView::LEFT);
		return;
	case EditorAction::VIEW_RIGHT:
		SetCameraView(CameraView::RIGHT);
		return;
	case EditorAction::VIEW_TOP:
		SetCameraView(CameraView::TOP);
		return;
	case EditorAction::VIEW_BOTTOM:
		SetCameraView(CameraView::BOTTOM);
		return;
	case EditorAction::RESET_CAMERA:
		cameraController_.Reset();
		frameRequest_ = FrameRequest::NONE;
		return;
	case EditorAction::RESET_TRANSFORMS:
		ResetSelectedTransforms();
		return;
	case EditorAction::DROP_TO_GROUND:
		DropSelectionToGround();
		return;
	case EditorAction::DROP_TO_SURFACE:
		DropSelectionToSurface();
		return;
	case EditorAction::ADD_COLLIDERS:
		ConfigureSelectedColliders(ColliderOperation::ADD_MISSING);
		return;
	case EditorAction::FIT_COLLIDERS:
		ConfigureSelectedColliders(ColliderOperation::FIT_ALL);
		return;
	case EditorAction::REMOVE_COLLIDERS:
		ConfigureSelectedColliders(ColliderOperation::REMOVE_ALL);
		return;
	case EditorAction::PLACE_COLLIDERS_RIGHT:
		PlaceSelectionNextToActive(ColliderPlacement::Face::RIGHT);
		return;
	case EditorAction::PLACE_COLLIDERS_LEFT:
		PlaceSelectionNextToActive(ColliderPlacement::Face::LEFT);
		return;
	case EditorAction::PLACE_COLLIDERS_ABOVE:
		PlaceSelectionNextToActive(ColliderPlacement::Face::ABOVE);
		return;
	case EditorAction::PLACE_COLLIDERS_BELOW:
		PlaceSelectionNextToActive(ColliderPlacement::Face::BELOW);
		return;
	case EditorAction::PLACE_COLLIDERS_FRONT:
		PlaceSelectionNextToActive(ColliderPlacement::Face::FRONT);
		return;
	case EditorAction::PLACE_COLLIDERS_BACK:
		PlaceSelectionNextToActive(ColliderPlacement::Face::BACK);
		return;

	case EditorAction::ALIGN_X:
		AlignSelection(AlignmentAxis::X);
		return;
	case EditorAction::ALIGN_Y:
		AlignSelection(AlignmentAxis::Y);
		return;
	case EditorAction::ALIGN_Z:
		AlignSelection(AlignmentAxis::Z);
		return;
	case EditorAction::DISTRIBUTE_X:
		DistributeSelection(AlignmentAxis::X);
		return;
	case EditorAction::DISTRIBUTE_Y:
		DistributeSelection(AlignmentAxis::Y);
		return;
	case EditorAction::DISTRIBUTE_Z:
		DistributeSelection(AlignmentAxis::Z);
		return;
	case EditorAction::SHOW_ALL_MODELS:
		ShowAllModels();
		return;
	case EditorAction::REVEAL_ACTIVE:
		hierarchyPanel_.RequestReveal(selection_.GetActiveId());
		return;
	case EditorAction::COPY_OBJECTS:
		CopySelectedObjects(false);
		return;
	case EditorAction::CUT_OBJECTS:
		CopySelectedObjects(true);
		return;
	case EditorAction::PASTE_OBJECTS:
		PasteObjects();
		return;
	case EditorAction::GIZMO_MOVE:
		gizmo_.SetOperation(TransformGizmo::Operation::TRANSLATE);
		return;
	case EditorAction::GIZMO_ROTATE:
		gizmo_.SetOperation(TransformGizmo::Operation::ROTATE);
		return;
	case EditorAction::GIZMO_SCALE:
		gizmo_.SetOperation(TransformGizmo::Operation::SCALE);
		return;
	case EditorAction::GIZMO_PIVOT:
		gizmo_.SetPivotMode(gizmo_.GetPivotMode() ==
		                            SelectionPivot::Mode::CENTER
		                        ? SelectionPivot::Mode::ACTIVE
		                        : SelectionPivot::Mode::CENTER);
		return;
	case EditorAction::GIZMO_SPACE:
		gizmo_.SetSpace(gizmo_.GetSpace() == TransformGizmo::Space::LOCAL
		                    ? TransformGizmo::Space::WORLD
		                    : TransformGizmo::Space::LOCAL);
		return;
	case EditorAction::SELECT_ALL: {
		if (hierarchySelectionScope_) {
			hierarchyPanel_.SelectAllVisible(document_.GetScene(), selection_);
		} else {
			std::vector<SceneObjectId> ids;

			for (const auto &[id, object] : document_.GetScene().GetObjects()) {
				if (object.IsRenderable()) {
					ids.push_back(id);
				}
			}

			selection_.Set(std::move(ids));
		}

		return;
	}
	case EditorAction::DESELECT_ALL:
		selection_.Clear();
		return;
	case EditorAction::FRAME_SELECTED:
		frameRequest_ = FrameRequest::SELECTED;
		return;
	case EditorAction::FRAME_ALL:
		frameRequest_ = FrameRequest::ALL;
		return;
	case EditorAction::UNDO:
		if (document_.GetHistory().CanUndo() &&
		    !document_.GetHistory().Undo()) {
			ShowError("Could not undo the edit.");
		}

		return;

	case EditorAction::REDO:
		if (document_.GetHistory().CanRedo() &&
		    !document_.GetHistory().Redo()) {
			ShowError("Could not redo the edit.");
		}

		return;

	case EditorAction::NEW:
	case EditorAction::NEW_SAMPLE:
	case EditorAction::OPEN:
	case EditorAction::OPEN_RECENT:
	case EditorAction::EXIT:
		// 保存確認が終わるまで、実際の操作は保留する
		pendingAction_ = action;

		if (action == EditorAction::EXIT &&
		    materialPanel_.HasUnsavedChanges()) {
			saveChangesDialog_.Show(
			    PathUtils::ToUtf8(materialPanel_.GetFilePath().filename()));
			stage_ = DialogStage::CONFIRM_MATERIAL_SAVE;
		} else {
			ConfirmSceneChanges();
		}

		return;

	case EditorAction::SAVE:
		pendingAction_ = EditorAction::NONE;
		pendingMaterialPath_.clear();
		StartSave(false);
		return;

	case EditorAction::SAVE_AS:
		pendingAction_ = EditorAction::NONE;
		pendingMaterialPath_.clear();
		StartSave(true);
		return;

	case EditorAction::RELOAD_SCENE:
		pendingAction_ = EditorAction::NONE;
		pendingMaterialPath_.clear();

		if (!document_.GetFilePath().empty()) {
			sceneReloadDialog_.Show(
			    PathUtils::ToUtf8(document_.GetFilePath().filename()));
			stage_ = DialogStage::CONFIRM_RELOAD;
		}

		return;

	case EditorAction::SAVE_COPY:
		pendingAction_ = EditorAction::NONE;
		pendingMaterialPath_.clear();
		StartSaveCopy();
		return;
	case EditorAction::CREATE_EMPTY:
		CreateObject(SceneObject{});
		return;
	case EditorAction::CREATE_BOX:
		CreateObject(PrimitiveObjects::CreateBox());
		return;
	case EditorAction::CREATE_CAMERA:
		CreateCameraFromView();
		return;

	case EditorAction::SNAP_POSITIONS:
		SnapSelectedPositions();
		return;

	case EditorAction::PASTE_CAMERA_LENSES:
		PasteCameraLensesToSelection();
		return;

	case EditorAction::PASTE_COLLIDERS:
		PasteCollidersToSelection();
		return;

	case EditorAction::DUPLICATE_ARRAY:
		StartObjectArray();
		return;

	case EditorAction::DUPLICATE_OBJECTS:
		DuplicateSelectedObjects();
		return;

	case EditorAction::DELETE_OBJECTS:
		DeleteSelectedObjects();
		return;
	case EditorAction::NONE:
		return;
	}
}

void EditorScene::OpenMaterialForEdit(const std::filesystem::path &path) {
	if (IsBusy() || path.empty()) {
		return;
	}

	try {
		FinishEdit();

		if (!materialPanel_.GetFilePath().empty() &&
		    PathUtils::IsSameFile(path, materialPanel_.GetFilePath())) {
			materialPanel_.RequestFocus();
			return;
		}

		pendingMaterialPath_ = path;

		if (materialPanel_.HasUnsavedChanges()) {
			saveChangesDialog_.Show(
			    PathUtils::ToUtf8(materialPanel_.GetFilePath().filename()));
			stage_ = DialogStage::CONFIRM_MATERIAL_OPEN;
		} else {
			CompleteMaterialOpen(false);
		}
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::OpenMaterialForEdit: {}", error.what()));
		ShowError("Could not open the material. Your edits were kept. See the "
		          "log for details.");
	}
}

void EditorScene::CompleteMaterialOpen(bool discardChanges) {
	try {
		if (materialPanel_.Open(pendingMaterialPath_, discardChanges)) {
			pendingMaterialPath_.clear();
			stage_ = DialogStage::IDLE;
			materialPanel_.RequestFocus();
		} else {
			ShowError("Could not open the material: " +
			          materialPanel_.GetError());
		}
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::CompleteMaterialOpen: {}", error.what()));
		ShowError("Could not open the material. Your edits were kept. See the "
		          "log for details.");
	}
}

void EditorScene::ConfirmSceneChanges() {
	if (document_.IsModified()) {
		const auto name =
		    document_.GetFilePath().empty()
		        ? std::string("Untitled")
		        : PathUtils::ToUtf8(document_.GetFilePath().filename());
		saveChangesDialog_.Show(name);
		stage_ = DialogStage::CONFIRM_SAVE;
	} else {
		ExecutePendingAction();
	}
}

void EditorScene::ExecutePendingAction() {
	const EditorAction action = pendingAction_;

	pendingAction_ = EditorAction::NONE;
	pendingMaterialPath_.clear();
	stage_ = DialogStage::IDLE;

	switch (action) {
	case EditorAction::EXPORT_GAME: {
		GameExportOptions options;
		const auto root = PathUtils::GetExecutableDirectory();
#if defined(_DEBUG)
		options.runtimeDirectory = root.parent_path() / L"Release";
#else
		options.runtimeDirectory = root;
#endif
		options.resourceDirectory = root / L"resources";
		options.destination = root / L"exports" / L"Game";
		options.config = exportConfig_;
		options.config.fallbackCamera = cameraController_.GetPose();
		exportDialog_.Show(std::move(options));
		stage_ = DialogStage::EXPORT_GAME;
		return;
	}

	case EditorAction::NEW_SAMPLE:
	case EditorAction::NEW: {
		try {
			auto objects = action == EditorAction::NEW_SAMPLE
			                   ? SampleStage::Create()
			                   : std::map<SceneObjectId, SceneObject>{};

			if (action == EditorAction::NEW_SAMPLE &&
			    !models_.Load(L"resources/editor/models/box.glb")) {
				ShowError("Could not load the sample box model. See the log "
				          "for details.");
				return;
			}

			document_.New();
			sceneCheckPanel_.Reset();
			frameRequest_ = FrameRequest::NONE;
			frameObject_ = 0;
			clipboard_.DetachExternalReferences();

			if (action == EditorAction::NEW_SAMPLE) {
				document_.GetScene().ReplaceObjects(std::move(objects));
				document_.MarkModified();
			}

			gizmo_.Cancel();
			gizmoEditActive_ = false;
			selection_.Clear();
			cameraController_.Reset();
			hierarchyPanel_.Clear();
			inspectorPanel_.Clear();
		} catch (const std::exception &error) {
			Logger::Error(error.what());
			ShowError("Could not create the scene. See the log for details.");
		}

		return;
	}

	case EditorAction::OPEN:
		fileDialog_.Show(SceneFileDialogMode::OPEN, document_.GetFilePath());

		stage_ = DialogStage::OPEN_FILE;
		return;
	case EditorAction::OPEN_RECENT:
		OpenScene(recentOpenPath_);
		return;

	case EditorAction::EXIT:
		closeApproved_ = true;
		return;

	default:
		return;
	}
}

void EditorScene::StartSave(bool saveAs) {
	if (!saveAs && !document_.GetFilePath().empty()) {
		if (document_.Save()) {
			recentScenes_.Add(document_.GetFilePath());
			ExecutePendingAction();
		} else {
			ShowError(document_.GetError());
		}

		return;
	}

	fileDialog_.Show(SceneFileDialogMode::SAVE, document_.GetFilePath());

	stage_ = DialogStage::SAVE_FILE;
}

void EditorScene::CreateCameraFromView() {
	const auto object =
	    CameraObjects::CreateFromView(cameraController_.GetPose());

	if (!object) {
		ShowError(
		    "The Scene view camera pose is invalid. No Camera was created.");
		return;
	}

	CreateObject(*object);
}

void EditorScene::CreateObject(SceneObject object) {
	try {
		FinishEdit();

		if (object.collider && object.HasMesh()) {
			BoxCollider fitted;

			if (!GetFittedCollider(object, fitted)) {
				ShowError("Could not load the built-in model. No object was "
				          "created.");
				return;
			}

			object.collider = fitted;
		}

		auto ids = document_.GetHistory().CreateObjects({std::move(object)});

		if (ids.empty()) {
			ShowError("Could not create the object.");
			return;
		}

		selection_.Set(std::move(ids));
		hierarchyPanel_.RequestReveal(selection_.GetActiveId());
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::CreateObject: {}", error.what()));
		ShowError("Could not create the object. See the log for details.");
	}
}

void EditorScene::FitCollider(SceneObjectId id) {
	try {
		FinishEdit();
		const auto *object = document_.GetScene().FindObject(id);

		if (!object)
			return;

		BoxCollider collider;

		if (!GetFittedCollider(*object, collider)) {
			ShowError("Could not fit the collider to the model. See the log "
			          "for details.");
			return;
		}

		if (!document_.GetHistory().SetCollider(id, collider)) {
			ShowError("Could not set a collider at this transform.");
		}
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::FitCollider: {}", error.what()));
		ShowError("Could not configure the collider. See the log for details.");
	}
}

bool EditorScene::GetFittedCollider(const SceneObject &object,
                                    BoxCollider &collider) {
	BoxCollider fitted;

	if (object.HasMesh()) {
		const auto model = models_.Load(object.mesh->modelPath);
		std::array<DirectX::XMFLOAT3, 8> corners;

		if (!models_.GetLocalBounds(model, corners) ||
		    !BoxColliderUtils::FitToPoints(corners, fitted)) {
			return false;
		}
	}

	collider = fitted;
	return true;
}

void EditorScene::ConfigureSelectedColliders(ColliderOperation operation) {
	if (!HasSelection()) {
		return;
	}

	try {
		FinishEdit();
		std::vector<EditorHistory::ColliderEdit> edits;
		edits.reserve(selection_.GetCount());

		for (const auto id : selection_.GetIds()) {
			const auto &object = *document_.GetScene().FindObject(id);

			if ((operation == ColliderOperation::ADD_MISSING &&
			     object.collider) ||
			    (operation == ColliderOperation::REMOVE_ALL &&
			     !object.collider)) {
				continue;
			}

			std::optional<BoxCollider> collider;

			if (operation != ColliderOperation::REMOVE_ALL) {
				BoxCollider fitted;

				if (!GetFittedCollider(object, fitted)) {
					ShowError(std::format("Could not fit a collider to '{}'. "
					                      "No colliders were changed.",
					                      object.name));
					return;
				}

				collider = fitted;
			}

			edits.push_back({id, collider});
		}

		if (!edits.empty() && !document_.GetHistory().SetColliders(edits)) {
			ShowError("Could not configure the selected colliders. No "
			          "colliders were changed.");
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene::ConfigureSelectedColliders: {}",
		                          error.what()));
		ShowError("Could not configure the selected colliders. See the log for "
		          "details.");
	}
}

void EditorScene::SelectModelInstances(const std::filesystem::path &path) {
	FinishEdit();

	try {
		const auto count =
		    ModelUsage::SelectObjects(document_.GetScene(), path, selection_);
		hierarchyPanel_.RequestReveal(selection_.GetActiveId());

		if (count == 0) {
			Logger::Info("このモデルを使用する物体はシーン内にありません。");
		}
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::SelectModelInstances: {}", error.what()));
		ShowError("Could not select model instances. See the log for details.");
	}
}

void EditorScene::SelectMaterialUsers(const std::filesystem::path &path) {
	FinishEdit();

	try {
		const auto count = MaterialUsage::SelectObjects(document_.GetScene(),
		                                                path, selection_);
		hierarchyPanel_.RequestReveal(selection_.GetActiveId());

		if (count == 0) {
			Logger::Info("この材質を割り当てた物体はシーン内にありません。");
		}
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::SelectMaterialUsers: {}", error.what()));
		ShowError("Could not select material users. See the log for details.");
	}
}

void EditorScene::SelectFollowingCameras() {
	FinishEdit();

	try {
		FollowCameraUsage::SelectObjects(document_.GetScene(), selection_);
		hierarchyPanel_.RequestReveal(selection_.GetActiveId());
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene::SelectFollowingCameras: {}",
		                          error.what()));
		ShowError("Could not select Follow Cameras. See the log for details.");
	}
}

void EditorScene::SetCameraView(CameraView view) {
	DirectX::XMFLOAT3 center{};

	if (selection_.GetCount() == 1) {
		const auto *object =
		    document_.GetScene().FindObject(selection_.GetActiveId());

		if (!object) {
			return;
		}

		center = object->transform.translation;
	} else if (selection_.GetCount() > 1) {
		Transform pivot;

		if (!SelectionPivot::Make(document_.GetScene(), selection_,
		                          SelectionPivot::Mode::CENTER, pivot)) {
			return;
		}

		center = pivot.translation;
	}

	if (!cameraController_.SetView(view, center)) {
		Logger::Warning(
		    "EditorScene: 指定した方向へカメラを切り替えられませんでした。");
		return;
	}

	frameRequest_ = FrameRequest::NONE;
}

void EditorScene::StartSaveCopy() {
	try {
		const auto &currentPath = document_.GetFilePath();
		auto copyPath = currentPath.empty()
		                    ? std::filesystem::path(L"Untitled.copy.scene.json")
		                    : currentPath.parent_path() /
		                          (currentPath.stem().native() + L".copy" +
		                           currentPath.extension().native());
		fileDialog_.Show(SceneFileDialogMode::SAVE_COPY, copyPath);
		stage_ = DialogStage::SAVE_COPY_FILE;
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::StartSaveCopy: {}", error.what()));
		ShowError(
		    "Could not select a copy destination. See the log for details.");
	}
}

void EditorScene::UpdateDialogs() {
	switch (stage_) {
	case DialogStage::IDLE:
		return;

	case DialogStage::CONFIRM_MATERIAL_OPEN: {
		const auto result = saveChangesDialog_.Draw(ui_);

		if (result == SaveChangesResult::SAVE) {
			try {
				if (materialPanel_.Save()) {
					CompleteMaterialOpen(false);
				} else {
					ShowError("Could not save the material: " +
					          materialPanel_.GetError());
				}
			} catch (const std::exception &error) {
				Logger::Error(
				    std::format("EditorScene::SaveMaterial: {}", error.what()));
				ShowError("Could not save the material. Your edits were kept. "
				          "See the log for details.");
			}
		} else if (result == SaveChangesResult::DISCARD) {
			CompleteMaterialOpen(true);
		} else if (result == SaveChangesResult::CANCEL) {
			pendingMaterialPath_.clear();
			stage_ = DialogStage::IDLE;
		}

		return;
	}

	case DialogStage::CONFIRM_MATERIAL_SAVE: {
		const auto result = saveChangesDialog_.Draw(ui_);

		if (result == SaveChangesResult::SAVE) {
			try {
				if (materialPanel_.Save()) {
					ContinueAfterMaterialConfirmation();
				} else {
					ShowError("Could not save the material: " +
					          materialPanel_.GetError());
				}
			} catch (const std::exception &error) {
				Logger::Error(
				    std::format("EditorScene::SaveMaterial: {}", error.what()));
				ShowError("Could not save the material. Your edits were kept. "
				          "See the log for details.");
			}
		} else if (result == SaveChangesResult::DISCARD) {
			// 出力の取消や後続のシーン確認に備え、材質の編集中の値は保持する。
			ContinueAfterMaterialConfirmation();
		} else if (result == SaveChangesResult::CANCEL) {
			pendingAction_ = EditorAction::NONE;
			pendingMaterialPath_.clear();
			stage_ = DialogStage::IDLE;
		}

		return;
	}

	case DialogStage::CONFIRM_SAVE: {
		const auto result = saveChangesDialog_.Draw(ui_);

		switch (result) {
		case SaveChangesResult::SAVE:
			StartSave(false);
			break;

		case SaveChangesResult::DISCARD:
			ExecutePendingAction();
			break;

		case SaveChangesResult::CANCEL:
			pendingAction_ = EditorAction::NONE;
			pendingMaterialPath_.clear();
			stage_ = DialogStage::IDLE;
			break;

		case SaveChangesResult::NONE:
			break;
		}

		return;
	}

	case DialogStage::OPEN_FILE: {
		const auto result = fileDialog_.Draw(ui_);

		if (result == SceneFileDialogResult::CANCELLED) {
			stage_ = DialogStage::IDLE;
			return;
		}

		if (result == SceneFileDialogResult::ACCEPTED) {
			OpenScene(fileDialog_.GetSelectedPath());
		}

		return;
	}

	case DialogStage::SAVE_FILE: {
		const auto result = fileDialog_.Draw(ui_);

		if (result == SceneFileDialogResult::CANCELLED) {
			// 保存先選択を取り消した場合は、NewやExitも取り消す
			pendingAction_ = EditorAction::NONE;
			pendingMaterialPath_.clear();
			stage_ = DialogStage::IDLE;
			return;
		}

		if (result == SceneFileDialogResult::ACCEPTED) {
			if (document_.SaveAs(fileDialog_.GetSelectedPath())) {
				recentScenes_.Add(document_.GetFilePath());
				ExecutePendingAction();
			} else {
				ShowError(document_.GetError());
			}
		}

		return;
	}

	case DialogStage::CONFIRM_RELOAD: {
		const auto result = sceneReloadDialog_.Draw(ui_);

		if (result == SceneReloadResult::CANCELLED) {
			stage_ = DialogStage::IDLE;
		} else if (result == SceneReloadResult::RELOAD) {
			OpenScene(document_.GetFilePath());
		}

		return;
	}

	case DialogStage::SAVE_COPY_FILE: {
		const auto result = fileDialog_.Draw(ui_);

		if (result == SceneFileDialogResult::CANCELLED) {
			stage_ = DialogStage::IDLE;
		} else if (result == SceneFileDialogResult::ACCEPTED) {
			if (document_.SaveCopy(fileDialog_.GetSelectedPath())) {
				recentScenes_.Add(fileDialog_.GetSelectedPath());
				stage_ = DialogStage::IDLE;
			} else {
				ShowError(document_.GetError());
			}
		}

		return;
	}

	case DialogStage::OBJECT_ARRAY: {
		const auto result = objectArrayDialog_.Draw(ui_);

		if (result == ObjectArrayResult::NONE) {
			return;
		}

		stage_ = DialogStage::IDLE;

		if (result == ObjectArrayResult::ACCEPTED) {
			try {
				auto ids = document_.GetHistory().DuplicateObjectsInArray(
				    objectArrayDialog_.GetSourceIds(),
				    objectArrayDialog_.GetSettings());

				if (ids.empty()) {
					ShowError("Could not create the offset copies. Check the "
					          "targets and coordinates.");
				} else {
					selection_.Set(std::move(ids));
					hierarchyPanel_.RequestReveal(selection_.GetActiveId());
				}
			} catch (const std::exception &error) {
				Logger::Error(
				    std::format("EditorScene::ObjectArray: {}", error.what()));
				ShowError("Could not create the offset copies. See the log for "
				          "details.");
			}
		}

		return;
	}

	case DialogStage::BATCH_RENAME: {
		const auto result = batchRenameDialog_.Draw(ui_);

		if (result == BatchRenameResult::NONE) {
			return;
		}

		stage_ = DialogStage::IDLE;

		if (result == BatchRenameResult::ACCEPTED) {
			try {
				if (!document_.GetHistory().RenameObjects(
				        batchRenameDialog_.GetRenames())) {
					ShowError("The rename targets changed. No object names "
					          "were changed.");
				}
			} catch (const std::exception &error) {
				Logger::Error(
				    std::format("EditorScene::BatchRename: {}", error.what()));
				ShowError(
				    "Could not rename the objects. See the log for details.");
			}
		}

		return;
	}

	case DialogStage::GAME_SETTINGS: {
		const auto result = gameSettingsDialog_.Draw(ui_);

		if (result == GameSettingsResult::ACCEPTED) {
			exportConfig_ = gameSettingsDialog_.GetConfig();
		}

		if (result != GameSettingsResult::NONE) {
			stage_ = DialogStage::IDLE;
		}

		return;
	}
	case DialogStage::EXPORT_GAME: {
		const auto result = exportDialog_.Draw(ui_);

		if (result == GameExportResult::CANCELLED) {
			stage_ = DialogStage::IDLE;
		}

		if (result == GameExportResult::ACCEPTED) {
			std::string error;

			if (GameExporter::Export(document_.GetScene(),
			                         exportDialog_.GetOptions(), error)) {
				Logger::Info(
				    "Game exported to " +
				    PathUtils::ToUtf8(exportDialog_.GetOptions().destination));
				exportConfig_ = exportDialog_.GetOptions().config;
				stage_ = DialogStage::IDLE;
			} else {
				ShowError(std::move(error));
			}
		}

		return;
	}
	case DialogStage::ERROR: {
		constexpr const char *kTitle = "Operation failed###SceneOperationError";

		if (errorOpenRequested_) {
			ui_.OpenPopup(kTitle);
			errorOpenRequested_ = false;
		}

		if (ui_.BeginPopupModal(kTitle)) {
			ui_.Text(errorMessage_);

			if (document_.GetScene().Contains(errorObjectId_)) {
				if (ui_.Button("Show Object")) {
					try {
						FinishEdit();
						selection_.SelectOnly(errorObjectId_);
						inspectorPanel_.Clear();
						inspectorPanel_.RequestInspect(errorObjectId_);
						hierarchyPanel_.RequestReveal(errorObjectId_);
						ui_.CloseCurrentPopup();
						errorObjectId_ = 0;
						stage_ = DialogStage::IDLE;
					} catch (const std::exception &error) {
						Logger::Error(std::format(
						    "EditorScene::ShowErrorObject: {}", error.what()));
						errorMessage_ = "Could not select the object. See the "
						                "log for details.";
					}
				}

				ui_.SameLine();
			}

			if (ui_.Button("OK")) {
				ui_.CloseCurrentPopup();
				errorObjectId_ = 0;
				stage_ = DialogStage::IDLE;
			}

			ui_.EndPopup();
		}

		return;
	}
	}
}

void EditorScene::OpenScene(const std::filesystem::path &path) {
	const bool opened = document_.Open(path, [this](const Scene &candidate) {
		for (const auto &[id, object] : candidate.GetObjects()) {
			// 非表示のモデルは、再表示を要求されたときに読み込む
			if (object.IsRenderable() &&
			    !models_.Load(object.mesh->modelPath)) {
				Logger::Error(
				    std::format("EditorScene::OpenScene: "
				                "物体{}のモデルを読み込めません: {}",
				                id, PathUtils::ToUtf8(object.mesh->modelPath)));
				return false;
			}
		}

		return true;
	});

	if (!opened) {
		ShowError("Could not open the scene. The current scene was kept. See "
		          "the log for details.");
		return;
	}

	sceneCheckPanel_.Reset();
	clipboard_.DetachExternalReferences();
	frameRequest_ = FrameRequest::NONE;
	frameObject_ = 0;
	FinishEdit();
	selection_.Clear();
	cameraController_.Reset();
	hierarchyPanel_.Clear();
	inspectorPanel_.Clear();
	recentScenes_.Add(document_.GetFilePath());
	stage_ = DialogStage::IDLE;
}

void EditorScene::ShowError(std::string message, SceneObjectId objectId) {
	errorMessage_ = std::move(message);
	errorObjectId_ = document_.GetScene().Contains(objectId) ? objectId : 0;
	errorOpenRequested_ = true;

	pendingAction_ = EditorAction::NONE;
	pendingMaterialPath_.clear();
	stage_ = DialogStage::ERROR;
}

bool EditorScene::IsBusy() const {
	return stage_ != DialogStage::IDLE || exitQueued_ || closeApproved_;
}

void EditorScene::ApplyInspectorRequest(
    const InspectorPanel::ObjectRequest &request) {
	if (request.action == InspectorPanel::ObjectAction::NONE) {
		return;
	}

	try {
		FinishEdit();
		const auto *object = document_.GetScene().FindObject(request.objectId);

		if (!object) {
			ShowError("Could not find the object to edit.");
			return;
		}

		if (request.action == InspectorPanel::ObjectAction::ADD_MESH ||
		    request.action == InspectorPanel::ObjectAction::REMOVE_MESH ||
		    request.action == InspectorPanel::ObjectAction::ADD_MESH_RENDERER ||
		    request.action ==
		        InspectorPanel::ObjectAction::REMOVE_MESH_RENDERER) {
			auto &history = document_.GetHistory();
			bool applied = false;

			switch (request.action) {
			case InspectorPanel::ObjectAction::ADD_MESH:
				applied = object->mesh ||
				          history.SetMesh(request.objectId, MeshComponent{});
				break;
			case InspectorPanel::ObjectAction::REMOVE_MESH:
				applied = history.SetMesh(request.objectId, std::nullopt);
				break;
			case InspectorPanel::ObjectAction::ADD_MESH_RENDERER:
				applied = object->meshRenderer ||
				          history.SetMeshRenderer(request.objectId,
				                                  MeshRendererComponent{});
				break;
			case InspectorPanel::ObjectAction::REMOVE_MESH_RENDERER:
				applied =
				    history.SetMeshRenderer(request.objectId, std::nullopt);
				break;
			default:
				break;
			}

			if (!applied) {
				ShowError(
				    "Could not change the Mesh or Mesh Renderer component.");
			}

			return;
		}

		if (request.action == InspectorPanel::ObjectAction::COPY_CAMERA_LENS) {
			if (!object->camera ||
			    !clipboard_.CopyCameraLens(object->camera->settings)) {
				ShowError("Could not copy invalid Camera lens settings.");
			}

			return;
		}

		if (request.action == InspectorPanel::ObjectAction::PASTE_CAMERA_LENS) {
			const auto *copied = clipboard_.GetCameraLens();

			if (!copied || !document_.GetHistory().SetCameraLens(
			                   request.objectId, *copied)) {
				ShowError("Could not paste Camera lens settings. Copy a valid "
				          "lens and check the target Camera settings.");
			}

			return;
		}

		if (request.action == InspectorPanel::ObjectAction::COPY_COLLIDER) {
			if (!object->collider ||
			    !clipboard_.CopyCollider(*object->collider)) {
				ShowError("Could not copy an invalid Box Collider.");
			}

			return;
		}

		if (request.action == InspectorPanel::ObjectAction::PASTE_COLLIDER) {
			const auto *copied = clipboard_.GetCollider();

			if (!copied || !document_.GetHistory().SetCollider(request.objectId,
			                                                   *copied)) {
				ShowError("Could not paste the Box Collider. Copy a valid "
				          "shape and check the target Transform.");
			}

			return;
		}

		if (request.action == InspectorPanel::ObjectAction::COPY_TRANSFORM) {
			if (!clipboard_.CopyTransform(object->transform)) {
				ShowError("Could not copy an invalid Transform.");
			}

			return;
		}

		if (request.action ==
		    InspectorPanel::ObjectAction::PASTE_RESPAWN_POSITION) {
			const auto *copied = clipboard_.GetTransform();

			if (!object->respawn || !copied) {
				ShowError("Copy a Transform and inspect an object with Respawn "
				          "before pasting its position.");
				return;
			}

			auto settings = *object->respawn;
			settings.useStartPosition = false;
			settings.position = copied->translation;

			if (!document_.GetHistory().SetRespawn(request.objectId,
			                                       settings)) {
				ShowError("Could not paste the Respawn position. Coordinates "
				          "must be within -100000 and 100000.");
			}

			return;
		}

		if (request.action == InspectorPanel::ObjectAction::FRAME_RESPAWN ||
		    request.action == InspectorPanel::ObjectAction::FRAME_COLLIDER) {
			std::array<DirectX::XMFLOAT3, 8> corners;
			const bool respawn =
			    request.action == InspectorPanel::ObjectAction::FRAME_RESPAWN;
			const bool ready =
			    respawn ? RespawnPreview::GetFrameBounds(*object, corners)
			            : ColliderBounds::GetFrameBounds(*object, corners);

			if (!ready) {
				ShowError(
				    respawn ? "Could not determine the respawn frame bounds."
				            : "Could not determine the collider frame bounds.");
				return;
			}

			frameObject_ = request.objectId;
			frameRequest_ =
			    respawn ? FrameRequest::RESPAWN : FrameRequest::COLLIDER;
			// ドックの別タブにある場合も、次のフレームでSceneの描画サイズを取得する。
			ImGui::SetWindowCollapsed("Scene", false);
			ImGui::SetWindowFocus("Scene");
			return;
		}

		if (request.action ==
		        InspectorPanel::ObjectAction::SELECT_RESPAWN_BLOCKERS ||
		    request.action ==
		        InspectorPanel::ObjectAction::SELECT_COLLIDER_OVERLAPS) {
			std::vector<SceneObjectId> objects;
			const bool respawn =
			    request.action ==
			    InspectorPanel::ObjectAction::SELECT_RESPAWN_BLOCKERS;
			bool inspected = false;

			if (respawn) {
				inspected = RespawnPreview::FindBlockingObjects(
				    document_.GetScene(), request.objectId, objects);
			} else {
				AxisAlignedBox body;
				inspected =
				    object->collider &&
				    BoxColliderUtils::GetWorldBounds(*object->collider,
				                                     object->transform, body) &&
				    CollisionWorld::FindOverlaps(document_.GetScene(), body,
				                                 objects, request.objectId);
			}

			if (!inspected) {
				ShowError(respawn
				              ? "Could not inspect the respawn colliders. "
				                "Check the collider shapes and transforms."
				              : "Could not inspect the overlapping colliders. "
				                "Check the collider shapes and transforms.");
				return;
			}

			if (!objects.empty()) {
				selection_.Set(std::move(objects));
				hierarchyPanel_.RequestReveal(selection_.GetActiveId());
			}

			return;
		}

		if (request.action == InspectorPanel::ObjectAction::SELECT_OBJECT) {
			selection_.SelectOnly(request.objectId);
			hierarchyPanel_.RequestReveal(request.objectId);
			return;
		}

		bool applied = false;
		const std::span<const SceneObjectId> ids{&request.objectId, 1};

		if (request.action == InspectorPanel::ObjectAction::PASTE_TRANSFORM) {
			if (const auto *copied = clipboard_.GetTransform()) {
				applied = document_.GetHistory().SetTransforms(ids, *copied);
			}
		} else if (request.action ==
		           InspectorPanel::ObjectAction::RESET_TRANSFORM) {
			applied = document_.GetHistory().ResetTransforms(ids);
		} else if (request.action ==
		           InspectorPanel::ObjectAction::ALIGN_CAMERA_VIEW) {
			applied = document_.GetHistory().AlignCameraView(
			    request.objectId, cameraController_.GetPose());
		} else if (request.action == InspectorPanel::ObjectAction::ADD_CAMERA &&
		           !object->camera) {
			applied = document_.GetHistory().SetCamera(request.objectId,
			                                           CameraComponent{});
		} else if (request.action ==
		               InspectorPanel::ObjectAction::MAKE_PRIMARY_CAMERA &&
		           object->camera) {
			auto camera = *object->camera;
			camera.primary = true;
			applied =
			    document_.GetHistory().SetCamera(request.objectId, camera);
		}

		if (!applied) {
			if (request.action ==
			    InspectorPanel::ObjectAction::ALIGN_CAMERA_VIEW) {
				ShowError("Could not align the Camera. Check its Transform and "
				          "Follow Camera target and settings.");
			} else if (request.action ==
			               InspectorPanel::ObjectAction::ADD_CAMERA ||
			           request.action ==
			               InspectorPanel::ObjectAction::MAKE_PRIMARY_CAMERA) {
				ShowError("Could not update the Camera. Check its component "
				          "settings.");
			} else {
				ShowError("Could not change the object's Transform.");
			}
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene::ApplyInspectorRequest: {}",
		                          error.what()));
		ShowError(
		    "Could not apply the Inspector action. See the log for details.");
	}
}

void EditorScene::RestoreHistory(std::size_t index) {
	try {
		FinishEdit();

		if (!document_.GetHistory().MoveTo(index)) {
			ShowError("Could not restore the selected history state. The scene "
			          "was not changed.");
		}
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::RestoreHistory: {}", error.what()));
		ShowError("Could not restore history. See the log for details.");
	}
}

void EditorScene::ShowAllModels() {
	try {
		std::vector<SceneObjectId> ids;

		for (const auto &[id, object] : document_.GetScene().GetObjects()) {
			if (object.IsHidden()) {
				ids.push_back(id);
			}
		}

		if (!ids.empty()) {
			ChangeVisibility(ids, true);
		}
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::ShowAllModels: {}", error.what()));
		ShowError("Could not show all models. See the log for details.");
	}
}

bool EditorScene::ChangeVisibility(std::span<const SceneObjectId> objectIds,
                                   bool visible) {
	try {
		FinishEdit();

		// 全モデルを確認してから、一括変更を履歴へ反映する
		for (const auto id : objectIds) {
			const auto *object = document_.GetScene().FindObject(id);

			if (!object) {
				ShowError("Could not find an object to change its visibility.");
				return false;
			}

			if (visible && object->IsHidden() && object->HasMesh() &&
			    !models_.Load(object->mesh->modelPath)) {
				ShowError(
				    "Could not load a model. Visibility was kept unchanged.");
				return false;
			}
		}

		if (!document_.GetHistory().SetVisibility(objectIds, visible)) {
			ShowError("Could not change model visibility.");
			return false;
		}

		return true;
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::ChangeVisibility: {}", error.what()));
		ShowError(
		    "Could not change model visibility. See the log for details.");
		return false;
	}
}

bool EditorScene::AssignModel(SceneObjectId objectId,
                              const std::filesystem::path &modelPath) {
	try {
		FinishEdit();

		if (!modelPath.empty() && !models_.Load(modelPath)) {
			ShowError("Could not load the model. The current model was kept.");
			return false;
		}

		if (!document_.GetHistory().SetModelPath(objectId, modelPath)) {
			ShowError("Could not change the object's model.");
			return false;
		}

		return true;
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::AssignModel: {}", error.what()));
		ShowError(
		    "Could not change the object's model. See the log for details.");
		return false;
	}
}

bool EditorScene::PlaceModel(const std::filesystem::path &modelPath,
                             const DirectX::XMFLOAT3 &position) {
	try {
		if (modelPath.empty()) {
			return false;
		}

		FinishEdit();

		const std::string name = PathUtils::ToUtf8(modelPath.stem());

		// 読み込みを確認してから、シーンと履歴へ追加する
		const auto model = models_.Load(modelPath);

		if (!model) {
			ShowError("Could not load the model. See the log for details.");
			return false;
		}

		SceneObject object;
		object.name = name;
		object.mesh = MeshComponent{modelPath};
		object.meshRenderer = MeshRendererComponent{};
		object.transform.translation = position;
		// 配置位置も追加時のスナップショットへ含め、1回のUndo・Redoで復元する
		auto ids = document_.GetHistory().CreateObjects({std::move(object)});

		if (ids.empty()) {
			ShowError("Could not create the model object.");
			return false;
		}

		selection_.Set(std::move(ids));
		return true;
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene::PlaceModel: {}", error.what()));

		ShowError("Could not place the model. See the log for details.");
		return false;
	}
}

bool EditorScene::PlaceDroppedModel(const std::filesystem::path &modelPath,
                                    const Ray &ray) {
	try {
		ModelPlacement::Surface surface;
		std::array<DirectX::XMFLOAT3, 8> bounds;
		DirectX::XMFLOAT3 position;

		if (!ModelPlacement::GetSurface(ray, document_.GetScene(), surface)) {
			ShowError("Could not determine the model placement surface.");
			return false;
		}

		const auto model = models_.Load(modelPath);

		if (!models_.GetLocalBounds(model, bounds) ||
		    !ModelPlacement::GetSurfaceTranslation(surface, bounds, position)) {
			ShowError(
			    "Could not determine the model bounds or placement position.");
			return false;
		}

		if (gizmo_.IsSnapping()) {
			DirectX::XMFLOAT3 snapped;

			if (!MathUtils::SnapToGrid(
			        position, gizmo_.GetSnapSettings().translation, snapped)) {
				ShowError("Could not snap the model placement position.");
				return false;
			}

			// 接地する軸の位置は保ち、面に沿う軸だけをスナップする。
			if (surface.onSurface) {
				if (surface.normal.x != 0.0f) {
					snapped.x = position.x;
				} else if (surface.normal.y != 0.0f) {
					snapped.y = position.y;
				} else {
					snapped.z = position.z;
				}
			}

			position = snapped;
		}

		return PlaceModel(modelPath, position);
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::PlaceDroppedModel: {}", error.what()));
		ShowError(
		    "Could not place the dropped model. See the log for details.");
		return false;
	}
}

void EditorScene::FinishEdit() {
	document_.GetHistory().CommitEdit();

	gizmo_.Cancel();
	gizmoEditActive_ = false;
	sceneViewPanel_.CancelSelection();
}

bool EditorScene::ApplySceneSelection(
    const SceneViewPanel::SelectionRequest &request,
    const DirectX::XMFLOAT4X4 &view, const DirectX::XMFLOAT4X4 &projection,
    bool runtimeScene) {
	const auto &scene =
	    runtimeScene ? playMode_.GetSession().GetScene() : document_.GetScene();
	auto &renderer = runtimeScene ? playSceneRenderer_ : renderer_;
	auto &selection = runtimeScene ? playSelection_ : selection_;
	using Action = SceneViewPanel::SelectionRequest::Action;

	if (request.action == Action::CLICK) {
		float modelDistance = (std::numeric_limits<float>::max)();
		SceneObjectId picked =
		    renderer.Pick(request.minimum.x, request.minimum.y, &modelDistance);
		Ray ray;

		if (showObjectMarkers_ &&
		    renderer.CreatePickingRay(request.minimum.x, request.minimum.y,
		                              ray)) {
			if (const auto marker =
			        ObjectMarkers::Pick(scene, ray, modelDistance)) {
				picked = marker;
			}
		}

		return request.additive ? selection.Toggle(picked)
		                        : selection.SelectOnly(picked);
	}

	if (request.action != Action::BOX) {
		return false;
	}

	SelectionFrustum volume;

	if (!volume.Initialize(view, projection, request.minimum,
	                       request.maximum)) {
		return false;
	}

	std::vector<SceneObjectId> ids;

	if (request.additive) {
		const auto current = selection.GetIds();
		ids.assign(current.begin(), current.end());
	}

	for (const auto &[id, object] : scene.GetObjects()) {
		std::array<DirectX::XMFLOAT3, 8> corners;
		const bool boundsReady =
		    (!object.HasMesh() || !object.meshRenderer)
		        ? showObjectMarkers_ &&
		              ObjectMarkers::GetBounds(object, corners)
		        : !object.IsHidden() && renderer.GetObjectBounds(id, corners);

		if (boundsReady && volume.Intersects(corners) &&
		    std::find(ids.begin(), ids.end(), id) == ids.end()) {
			ids.push_back(id);
		}
	}

	const auto current = selection.GetIds();

	if (std::equal(current.begin(), current.end(), ids.begin(), ids.end())) {
		return false;
	}

	return selection.Set(std::move(ids));
}

bool EditorScene::MakeSelectionTransform(Transform &transform) const {
	return SelectionPivot::Make(document_.GetScene(), selection_,
	                            gizmo_.GetPivotMode(), transform);
}

EditorScene::FrameRequest
EditorScene::ReadFrameShortcut(const ViewportInput &viewport,
                               bool hierarchyFocused) const {
	if (IsBusy() || !viewport.applicationFocused ||
	    (!viewport.windowFocused && !hierarchyFocused) ||
	    viewport.widgetActive || viewport.rightDown || viewport.middleDown ||
	    ui_.IsTextInputActive() || ImGui::IsAnyItemActive() ||
	    gizmo_.IsUsing() || sceneViewPanel_.IsSelecting() ||
	    document_.GetHistory().IsEditing()) {
		return FrameRequest::NONE;
	}

	const Keyboard &keyboard = input_.GetKeyboard();

	if (keyboard.IsDown(Key::CTRL) || keyboard.IsDown(Key::ALT) ||
	    !keyboard.IsPressed(Key::F)) {
		return FrameRequest::NONE;
	}

	return keyboard.IsDown(Key::SHIFT) ? FrameRequest::ALL
	                                   : FrameRequest::SELECTED;
}

bool EditorScene::FrameObjects(FrameRequest request,
                               const DirectX::XMFLOAT4X4 &projection,
                               bool runtimeScene) {
	const auto &scene =
	    runtimeScene ? playMode_.GetSession().GetScene() : document_.GetScene();

	if (request == FrameRequest::RESPAWN || request == FrameRequest::COLLIDER) {
		const auto *object = scene.FindObject(frameObject_);
		std::array<DirectX::XMFLOAT3, 8> corners;

		if (!object ||
		    !(request == FrameRequest::RESPAWN
		          ? RespawnPreview::GetFrameBounds(*object, corners)
		          : ColliderBounds::GetFrameBounds(*object, corners))) {
			return false;
		}

		if (!cameraController_.FrameBounds(corners, projection)) {
			Logger::Warning(
			    "EditorScene: "
			    "コンポーネントの範囲を現在の描画範囲に収められませんでした。");
			return false;
		}

		return true;
	}

	auto &renderer = runtimeScene ? playSceneRenderer_ : renderer_;
	const auto &selection = runtimeScene ? playSelection_ : selection_;
	DirectX::XMFLOAT3 minimum{};
	DirectX::XMFLOAT3 maximum{};
	bool found = false;

	for (const auto &[id, object] : scene.GetObjects()) {
		if ((object.IsHidden() && object.HasMesh()) ||
		    (request == FrameRequest::SELECTED && !selection.Contains(id))) {
			continue;
		}

		std::array<DirectX::XMFLOAT3, 8> corners;

		const bool boundsReady = (!object.HasMesh() || !object.meshRenderer)
		                             ? ObjectFrameBounds::Get(object, corners)
		                             : renderer.GetObjectBounds(id, corners);

		if (!boundsReady) {
			continue;
		}

		for (const auto &corner : corners) {
			if (!found) {
				minimum = maximum = corner;
				found = true;
			} else {
				minimum.x = (std::min)(minimum.x, corner.x);
				minimum.y = (std::min)(minimum.y, corner.y);
				minimum.z = (std::min)(minimum.z, corner.z);
				maximum.x = (std::max)(maximum.x, corner.x);
				maximum.y = (std::max)(maximum.y, corner.y);
				maximum.z = (std::max)(maximum.z, corner.z);
			}
		}
	}

	if (!found) {
		return false;
	}

	std::array<DirectX::XMFLOAT3, 8> corners;

	for (std::size_t i = 0; i < corners.size(); ++i) {
		corners[i] = {
		    (i & 1) ? maximum.x : minimum.x,
		    (i & 2) ? maximum.y : minimum.y,
		    (i & 4) ? maximum.z : minimum.z,
		};
	}

	if (!cameraController_.FrameBounds(corners, projection)) {
		Logger::Warning(
		    "EditorScene: 対象物体を現在の描画範囲に収められませんでした。");
		return false;
	}

	return true;
}

bool EditorScene::UpdatePlaySceneView(float deltaTime, EditorAction action,
                                      bool hierarchyFocused) {
	ViewportInput viewport = sceneViewPanel_.GetInput();

	if (IsBusy() || ui_.IsTextInputActive() || ImGui::IsAnyItemActive() ||
	    sceneViewPanel_.IsSelecting()) {
		viewport = {};
	}

	ViewportInput frameInput = viewport;

	if (hierarchyFocused) {
		frameInput.applicationFocused = !ImGui::GetIO().AppFocusLost;
	}

	const auto requestedFrame =
	    action == EditorAction::FRAME_SELECTED ? FrameRequest::SELECTED
	    : action == EditorAction::FRAME_ALL
	        ? FrameRequest::ALL
	        : ReadFrameShortcut(frameInput, hierarchyFocused);

	if (requestedFrame != FrameRequest::NONE) {
		playFrameRequest_ = requestedFrame;
	}

	const auto &runtime = playMode_.GetSession().GetScene();
	const auto *active = runtime.FindObject(playSelection_.GetActiveId());
	cameraController_.Update(input_, viewport, deltaTime,
	                         active ? &active->transform.translation : nullptr);
	auto *target = sceneViewPanel_.GetRenderTexture();

	if (!target) {
		return true;
	}

	if (!playSceneRenderer_.SetCameraPose(cameraController_.GetPose()) ||
	    !playSceneRenderer_.Update(target->GetWidth(), target->GetHeight())) {
		return false;
	}

	DirectX::XMFLOAT4X4 view, projection;

	if (!playSceneRenderer_.GetCameraMatrices(view, projection)) {
		return false;
	}

	const auto selectionRequest =
	    sceneViewPanel_.UpdateSelection(ui_, !IsBusy(), false);

	try {
		if (ApplySceneSelection(selectionRequest, view, projection, true)) {
			playHierarchyPanel_.RequestReveal(playSelection_.GetActiveId());
		}
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::SelectRuntimeObject: {}", error.what()));
		ShowError("Could not select runtime objects. See the log for details.");
	}

	const auto frameRequest = playFrameRequest_;
	playFrameRequest_ = FrameRequest::NONE;

	if (frameRequest != FrameRequest::NONE &&
	    FrameObjects(frameRequest, projection, true)) {
		if (!playSceneRenderer_.SetCameraPose(cameraController_.GetPose()) ||
		    !playSceneRenderer_.Update(target->GetWidth(),
		                               target->GetHeight())) {
			return false;
		}
	}

	if ((showGrid_ && !sceneGrid_.Update(playSceneRenderer_)) ||
	    !colliderBounds_.Update(playSceneRenderer_, runtime,
	                            playSelection_.GetIds(), colliderDisplay_) ||
	    (showObjectMarkers_ &&
	     !objectMarkers_.Update(playSceneRenderer_, runtime,
	                            playSelection_.GetIds()))) {
		return false;
	}

	if (showSelectionBounds_) {
		for (const auto id : playSelection_.GetIds()) {
			if (!selectionBounds_.Update(playSceneRenderer_, id)) {
				return false;
			}
		}
	}

	playSceneReady_ = playSceneRenderer_.UpdateDebugLines();
	return playSceneReady_;
}

void EditorScene::StopAndKeepSelectedTransforms() {
	if (!playMode_.IsPlaying() || !playSelectionActive_ ||
	    playSelection_.GetCount() == 0) {
		return;
	}

	try {
		const auto &runtime = playMode_.GetSession().GetScene();
		std::vector<EditorHistory::TransformEdit> edits;
		edits.reserve(playSelection_.GetCount());

		for (const auto id : playSelection_.GetIds()) {
			const auto *object = runtime.FindObject(id);

			if (!object) {
				ShowError("Could not find every selected runtime object. "
				          "Play and the editing scene were kept.");
				return;
			}

			edits.push_back({id, object->transform});
		}

		FinishEdit();

		if (!document_.GetHistory().SetTransforms(edits)) {
			ShowError("Could not keep all selected Transforms. Check the "
			          "runtime and editing objects. Play and the editing scene "
			          "were kept.");
			return;
		}

		playMode_.Stop();
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "EditorScene::StopAndKeepSelectedTransforms: {}", error.what()));
		ShowError("Could not keep the runtime Transforms. Play was kept. "
		          "See the log for details.");
	}
}

void EditorScene::ClearPlaySelection() {
	sceneViewPanel_.CancelSelection();
	playSelection_.Clear();
	playHierarchyPanel_.Clear();
	playSelectionActive_ = false;
	playFrameRequest_ = FrameRequest::NONE;
}

void EditorScene::DuplicateSelectedObjects() {
	if (!HasSelection()) {
		return;
	}

	try {
		auto createdIds =
		    document_.GetHistory().DuplicateObjects(selection_.GetIds());

		if (createdIds.empty()) {
			ShowError("Could not duplicate the selected objects.");
			return;
		}

		if (!selection_.Set(std::move(createdIds))) {
			ShowError("Could not select the duplicated objects.");
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "EditorScene: 物体の一括複製に失敗しました: {}", error.what()));

		ShowError("Could not duplicate the selected objects. "
		          "See the log for details.");
	}
}

void EditorScene::DeleteSelectedObjects() {
	if (!HasSelection()) {
		return;
	}

	try {
		if (!document_.GetHistory().DeleteObjects(selection_.GetIds())) {
			ShowError("Could not delete the selected objects.");
			return;
		}

		selection_.Clear();
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "EditorScene: 物体の一括削除に失敗しました: {}", error.what()));

		ShowError("Could not delete the selected objects. "
		          "See the log for details.");
	}
}

void EditorScene::CopySelectedObjects(bool cut) {
	if (!HasSelection()) {
		return;
	}

	try {
		if (!clipboard_.Copy(document_.GetScene(), selection_.GetIds())) {
			ShowError("Could not copy the selected objects.");
			return;
		}

		if (cut) {
			DeleteSelectedObjects();
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene: 物体のコピーに失敗しました: {}",
		                          error.what()));
		ShowError("Could not copy objects. See the log for details.");
	}
}

void EditorScene::PasteObjects() {
	if (!clipboard_.HasObjects()) {
		return;
	}

	try {
		const auto copied = clipboard_.GetObjects();
		std::vector<SceneObject> objects(copied.begin(), copied.end());

		for (auto &object : objects) {
			object.name += " Copy";
		}

		auto ids = document_.GetHistory().CreateObjects(
		    std::move(objects), clipboard_.GetSourceIds());

		if (ids.empty()) {
			ShowError("Could not paste the copied objects.");
			return;
		}

		selection_.Set(std::move(ids));
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "EditorScene: 物体の貼り付けに失敗しました: {}", error.what()));
		ShowError("Could not paste objects. See the log for details.");
	}
}

void EditorScene::ResetSelectedTransforms() {
	if (!HasSelection()) {
		return;
	}

	try {
		if (!document_.GetHistory().ResetTransforms(selection_.GetIds())) {
			ShowError("Could not reset the selected transforms.");
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "EditorScene: Transformの初期化に失敗しました: {}", error.what()));
		ShowError("Could not reset transforms. See the log for details.");
	}
}

void EditorScene::SnapSelectedPositions() {
	if (!HasSelection()) {
		return;
	}

	try {
		FinishEdit();

		if (!document_.GetHistory().SnapObjectsToGrid(
		        selection_.GetIds(), gizmo_.GetSnapSettings().translation)) {
			ShowError("Could not snap all selected positions to the grid. "
			          "Check the Transforms and move snap intervals. No "
			          "positions were changed.");
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene::SnapSelectedPositions: {}",
		                          error.what()));
		ShowError("Could not snap positions. See the log for details.");
	}
}

void EditorScene::StartBatchRename() {
	if (!HasSelection()) {
		return;
	}

	try {
		FinishEdit();

		if (!batchRenameDialog_.Show(document_.GetScene(),
		                             selection_.GetIds())) {
			ShowError("Could not prepare the selected objects for renaming.");
			return;
		}

		stage_ = DialogStage::BATCH_RENAME;
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::StartBatchRename: {}", error.what()));
		ShowError("Could not prepare object names. See the log for details.");
	}
}

void EditorScene::AlignSelection(AlignmentAxis axis) {
	if (!HasSelection() || selection_.GetCount() < 2) {
		return;
	}

	try {
		FinishEdit();

		if (!document_.GetHistory().AlignObjects(
		        selection_.GetIds(), selection_.GetActiveId(), axis)) {
			ShowError("Could not align the selected objects.");
		}
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::AlignSelection: {}", error.what()));
		ShowError(
		    "Could not align the selected objects. See the log for details.");
	}
}

void EditorScene::DistributeSelection(AlignmentAxis axis) {
	if (!HasSelection() || selection_.GetCount() < 3) {
		return;
	}

	try {
		FinishEdit();

		if (!document_.GetHistory().DistributeObjects(selection_.GetIds(),
		                                              axis)) {
			ShowError("Could not distribute the selected objects.");
		}
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::DistributeSelection: {}", error.what()));
		ShowError("Could not distribute the selected objects. See the log for "
		          "details.");
	}
}

bool EditorScene::HasVisibleSelectedModel() const {
	if (!HasSelection()) {
		return false;
	}

	const auto &scene = document_.GetScene();
	return std::any_of(selection_.GetIds().begin(), selection_.GetIds().end(),
	                   [&](auto id) {
		                   const auto *object = scene.FindObject(id);
		                   return object && object->IsRenderable();
	                   });
}

void EditorScene::DropSelectionToGround() {
	if (!HasVisibleSelectedModel()) {
		return;
	}

	try {
		FinishEdit();

		// 同じフレームで確定した変形も、外接ボックスの計算へ反映する
		if (!renderer_.Update()) {
			ShowError(
			    "Could not update model bounds. The selection was not moved.");
			return;
		}

		std::vector<std::array<DirectX::XMFLOAT3, 8>> bounds;
		bounds.reserve(selection_.GetCount());

		for (const auto id : selection_.GetIds()) {
			const auto *object = document_.GetScene().FindObject(id);

			if (!object || !object->IsRenderable()) {
				continue;
			}

			std::array<DirectX::XMFLOAT3, 8> corners;

			if (!renderer_.GetObjectBounds(id, corners)) {
				ShowError("Could not read selected model bounds. The selection "
				          "was not moved.");
				return;
			}

			bounds.push_back(corners);
		}

		DirectX::XMFLOAT3 translation;

		if (!ModelPlacement::GetGroundTranslation(bounds, translation) ||
		    !document_.GetHistory().TranslateObjects(selection_.GetIds(),
		                                             translation)) {
			ShowError("Could not move the selection to the ground.");
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene::DropSelectionToGround: {}",
		                          error.what()));
		ShowError("Could not move the selection to the ground. See the log for "
		          "details.");
	}
}

void EditorScene::DropSelectionToSurface() {
	if (!HasSelection()) {
		return;
	}

	try {
		FinishEdit();
		DirectX::XMFLOAT3 translation;
		std::string error;

		if (!ColliderPlacement::GetDropTranslation(document_.GetScene(),
		                                           selection_.GetIds(),
		                                           translation, error)) {
			ShowError(std::move(error));
			return;
		}

		if (!document_.GetHistory().TranslateObjects(selection_.GetIds(),
		                                             translation)) {
			ShowError("Could not drop the selection to a collider surface.");
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene::DropSelectionToSurface: {}",
		                          error.what()));
		ShowError("Could not drop the selection to a collider surface. See the "
		          "log for details.");
	}
}

void EditorScene::PlaceSelectionNextToActive(ColliderPlacement::Face face) {
	if (!HasSelection() || selection_.GetCount() < 2) {
		return;
	}

	try {
		FinishEdit();
		DirectX::XMFLOAT3 translation;
		std::string error;
		const auto reference = selection_.GetActiveId();

		if (!ColliderPlacement::GetAdjacentTranslation(
		        document_.GetScene(), selection_.GetIds(), reference, face,
		        translation, error)) {
			ShowError(std::move(error));
			return;
		}

		std::vector<SceneObjectId> moving;
		moving.reserve(selection_.GetCount() - 1);

		for (const auto id : selection_.GetIds()) {
			if (id != reference) {
				moving.push_back(id);
			}
		}

		if (!document_.GetHistory().TranslateObjects(moving, translation)) {
			ShowError("Could not place selected colliders next to the active "
			          "object.");
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene::PlaceSelectionNextToActive: {}",
		                          error.what()));
		ShowError(
		    "Could not place selected colliders. See the log for details.");
	}
}

bool EditorScene::HasSelection() const {
	if (selection_.GetCount() == 0) {
		return false;
	}

	const Scene &scene = document_.GetScene();

	for (const SceneObjectId id : selection_.GetIds()) {
		if (!scene.Contains(id)) {
			return false;
		}
	}

	return true;
}

bool EditorScene::HasSingleSelection() const {
	return selection_.GetCount() == 1 &&
	       document_.GetScene().Contains(selection_.GetActiveId());
}

void EditorScene::StartObjectArray() {
	if (IsBusy() || !HasSelection()) {
		return;
	}

	try {
		FinishEdit();

		if (!objectArrayDialog_.Show(document_.GetScene(),
		                             selection_.GetIds())) {
			ShowError("Could not prepare the selected objects for offset "
			          "duplication.");
			return;
		}

		stage_ = DialogStage::OBJECT_ARRAY;
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::StartObjectArray: {}", error.what()));
		ShowError(
		    "Could not prepare offset duplication. See the log for details.");
	}
}

void EditorScene::PasteCollidersToSelection() {
	const auto *collider = clipboard_.GetCollider();

	if (!HasSelection() || !collider) {
		return;
	}

	try {
		FinishEdit();
		std::vector<EditorHistory::ColliderEdit> edits;
		edits.reserve(selection_.GetCount());

		for (const auto id : selection_.GetIds()) {
			edits.push_back({id, *collider});
		}

		if (!document_.GetHistory().SetColliders(edits)) {
			ShowError("Could not paste all Box Colliders. Check the selected "
			          "shapes and Transforms. No colliders were changed.");
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene::PasteCollidersToSelection: {}",
		                          error.what()));
		ShowError(
		    "Could not paste the Box Colliders. See the log for details.");
	}
}

void EditorScene::PasteCameraLensesToSelection() {
	const auto *settings = clipboard_.GetCameraLens();

	if (!HasSelection() || !settings) {
		return;
	}

	try {
		FinishEdit();

		if (!document_.GetHistory().SetCameraLenses(selection_.GetIds(),
		                                            *settings)) {
			ShowError("Could not paste all Camera lenses. Check the selected "
			          "objects and Camera settings. No cameras were changed.");
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "EditorScene::PasteCameraLensesToSelection: {}", error.what()));
		ShowError(
		    "Could not paste the Camera lenses. See the log for details.");
	}
}

void EditorScene::ContinueAfterMaterialConfirmation() {
	if (pendingAction_ != EditorAction::EXPORT_GAME) {
		ConfirmSceneChanges();
		return;
	}

	try {
		ExecutePendingAction();
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::ContinueExport: {}", error.what()));
		ShowError("Could not open game export. Your edits were kept. See the "
		          "log for details.");
	}
}

void EditorScene::AssignMaterialToSelection(
    const MaterialPanel::AssignmentRequest &request) {
	if (!HasSelection()) {
		return;
	}

	try {
		FinishEdit();

		if (!document_.GetHistory().SetMaterialPaths(
		        selection_.GetIds(), request.slot, request.path)) {
			ShowError("Could not assign the material to all selected objects. "
			          "Every target needs valid Mesh Renderer settings. No "
			          "assignments were changed.");
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene::AssignMaterialToSelection: {}",
		                          error.what()));
		ShowError("Could not assign the material. See the log for details.");
	}
}
