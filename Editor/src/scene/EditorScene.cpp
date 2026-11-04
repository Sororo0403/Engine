#include "scene/EditorScene.h"

#include "assets/AssetFolders.h"
#include "assets/ModelPlacement.h"
#include "assets/ModelUsage.h"
#include "assets/ScenePlacement.h"
#include "assets/TextureUsage.h"
#include "camera/CameraObjects.h"
#include "camera/ObjectFrameBounds.h"
#include "core/EngineContext.h"
#include "core/Logger.h"
#include "gameplay/CharacterController.h"
#include "gameplay/FollowCameraUsage.h"
#include "gameplay/PlayShortcuts.h"
#include "gameplay/SampleStage.h"
#include "graphics/model/ModelManager.h"
#include "graphics/render/RenderSystem.h"
#include "graphics/render/RenderTexture.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "input/InputSystem.h"
#include "input/Key.h"
#include "math/MathUtils.h"
#include "math/Ray.h"
#include "math/TransformUtils.h"
#include "physics/BoxColliderUtils.h"
#include "physics/ColliderArrayPreview.h"
#include "physics/ColliderPlacement.h"
#include "physics/CollisionWorld.h"
#include "platform/windows/PathUtils.h"
#include "scene/PrimitiveObjects.h"
#include "scene/Scene.h"
#include "scene/SceneObjects.h"
#include "selection/SelectionFrustum.h"
#include "selection/SelectionMeasurement.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <format>
#include <imgui.h>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

EditorScene::EditorScene(const EngineContext &context)
    : ui_(context.ui), input_(context.input), models_(context.models),
      materials_(context.render.GetMaterials()),
      renderer_(document_.GetScene(), context.models, context.render,
                SceneRenderer::ModelLoadFailure::SKIP),
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
	cameraBookmarks_ = preferences_.cameraBookmarks;
	assetSources_ = preferences_.assets;
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
	showJumpGuide_ = preferences_.showJumpGuide;
	showMotionTrail_ = preferences_.showMotionTrail;
	playMode_.SetMaximizeOnPlay(preferences_.maximizeGameOnPlay);
	showObjectMarkers_ = preferences_.showObjectMarkers;
	showMeasurements_ = preferences_.showMeasurements;
	colliderDisplay_ = preferences_.colliderDisplay;
	gizmo_.SetSnapSettings(preferences_.snap);
	gizmo_.SetSnapping(preferences_.snapping);
	gizmo_.SetOperation(preferences_.operation);
	gizmo_.SetSpace(preferences_.space);
	gizmo_.SetPivotMode(preferences_.pivotMode);

	cameraController_.Reset();
	cameraController_.SetSettings(preferences_.camera);
	sceneProjection_ = preferences_.sceneProjection;

	stage_ = DialogStage::IDLE;
	pendingAction_ = EditorAction::NONE;
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
	pendingPlayCamera_ = 0;
	playMode_.SetPreviewCamera(0);
	inspectorPanel_.Clear();
	assetsPanel_.Clear();

	// 未接続の保存先も保持し、別のフォルダへ取り込んでしまうことを防ぐ。
	assets_ = AssetDatabase(assetSources_.GetDirectory());
	if (assets_.Refresh()) {
		assetSources_.Select(assets_.GetRootDirectory());
	}
	clipboard_.Clear();

	recoveryElapsed_ = 0;
	recoveryWasDirty_ = false;
	pendingRecoveryPath_.clear();
	restoredRecoveryPath_.clear();
	std::string recoveryError;
	if (!recovery_.Initialize(L"config/recovery", recoveryError)) {
		Logger::Error("EditorScene::Recovery: " + recoveryError);
	} else {
		std::vector<SceneRecovery::Entry> entries;
		if (!recovery_.Enumerate(entries, recoveryError)) {
			Logger::Error("EditorScene::Recovery: " + recoveryError);
		} else if (!entries.empty()) {
			recoveryDialog_.Show(std::move(entries));
			stage_ = DialogStage::RECOVERY;
		}
	}

	Logger::Info("EditorSceneを初期化しました。");
	return true;
}

bool EditorScene::Update(float deltaTime) {
	if (!std::isfinite(deltaTime) || deltaTime < 0.0f) {
		Logger::Error("EditorScene::Update: 経過時間が無効です。");
		return false;
	}

	UpdateRecovery(deltaTime);

	playFrame_ = false;
	playSceneReady_ = false;

	if (!playMode_.IsPlaying() && playSelectionActive_) {
		ClearPlaySelection();
	}

	performancePanel_.Record(deltaTime);
	gizmo_.BeginFrame();

	windowFocusRequest_ = nullptr;
	EditorAction action = DrawMenuBar();
	dockLayout_.Draw(ui_, resetDockLayout_);
	resetDockLayout_ = false;

	if (exitQueued_ && playMode_.IsPlaying()) {
		playMode_.Stop();
	}

	auto playAction = playMode_.DrawUI(ui_, !IsBusy(), exportConfig_,
	                                   !IsBusy() && playSelectionActive_ &&
	                                       playSelection_.GetCount() != 0,
	                                   &document_.GetScene());
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

	const auto requestedCamera =
	    std::exchange(pendingPlayCamera_, SceneObjectId{0});
	const bool useRequestedCamera =
	    requestedCamera != 0 && playAction == EditorPlayMode::Action::NONE &&
	    action == EditorAction::NONE && !IsBusy() && !playMode_.IsPlaying() &&
	    !exitQueued_;

	if (useRequestedCamera) {
		playAction = EditorPlayMode::Action::PLAY;
	}

	if (playAction == EditorPlayMode::Action::STOP_AND_KEEP_TRANSFORMS &&
	    !IsBusy()) {
		StopAndKeepSelectedTransforms();
	}

	if (playAction == EditorPlayMode::Action::STOP_AND_SELECT && !IsBusy()) {
		StopAndSelectEditingObjects();
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
		                     &invalidObject,
		                     useRequestedCamera ? requestedCamera : 0)) {
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

	if (playMode_.IsPlaying() && playMode_.IsMaximized()) {
		playFrame_ = true;
		sceneViewPanel_.CancelSelection();

		if (!playMode_.Update(deltaTime, input_, IsBusy())) {
			playMode_.Stop();
			ShowError("Play stopped because simulation or Game rendering "
			          "failed. Your editing scene is unchanged.");
		}

		if (showMotionTrail_) {
			motionTrail_.Record(playMode_.GetSession());
		} else {
			motionTrail_.Clear();
		}

		UpdateDialogs();

		if (!playMode_.IsPlaying()) {
			ClearPlaySelection();
		}

		return true;
	}

	if (playMode_.IsPlaying()) {
		playFrame_ = true;

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

		if (showMotionTrail_) {
			motionTrail_.Record(playMode_.GetSession());
		} else {
			motionTrail_.Clear();
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

		ui_.BeginDisabled(IsBusy());
		const auto stateRequest =
		    playStatePanel_.Draw(ui_, playMode_.GetSession());
		ui_.EndDisabled();

		if (!IsBusy() && playMode_.IsPlaying() && playSelectionActive_ &&
		    runtime.Contains(stateRequest.objectId)) {
			try {
				playSelection_.SelectOnly(stateRequest.objectId);
				playHierarchyPanel_.RequestReveal(stateRequest.objectId);

				if (stateRequest.frame) {
					playFrameRequest_ = FrameRequest::INSPECTED;
					ShowSceneView();
				}
			} catch (const std::exception &error) {
				Logger::Error(std::format(
				    "EditorScene::SelectPlayStateObject: {}", error.what()));
				ShowError("Could not select the runtime object.");
			}
		}

		measurementsPanel_.Draw(ui_, playMode_.GetSession().GetScene(),
		                        playSelection_.GetIds(), showMeasurements_);
		const auto *performanceTarget = sceneViewPanel_.GetRenderTexture();
		performancePanel_.Draw(
		    ui_, runtime, playSelection_,
		    performanceTarget ? performanceTarget->GetWidth() : 0,
		    performanceTarget ? performanceTarget->GetHeight() : 0);

		if (windowFocusRequest_) {
			ui_.FocusWindow(windowFocusRequest_);
		}

		DrawGridSettings();
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

	auto checkedRequest =
	    sceneCheckPanel_.Draw(ui_, scene, cameraController_.GetPose());
	const bool hasCheckRequest = !checkedRequest.objectIds.empty();

	if (!wasBusy && hasCheckRequest) {
		FinishEdit();
		if (selection_.Set(std::move(checkedRequest.objectIds))) {
			hierarchyPanel_.RequestReveal(selection_.GetActiveId());

			if (checkedRequest.inspect) {
				inspectorPanel_.RequestInspect(selection_.GetActiveId());
			}

			if (checkedRequest.frame) {
				frameRequest_ = FrameRequest::ISSUES;
				ShowSceneView();
			}
		}
	}

	const auto hierarchyRequest = hierarchyPanel_.Draw(
	    ui_, scene, selection_, true, clipboard_.GetCollider() != nullptr,
	    clipboard_.GetCameraLens() != nullptr,
	    clipboard_.GetRespawnSettings() != nullptr);

	if (hierarchyRequest.selectionChanged) {
		FinishEdit();
	}

	// ギズモ操作とInspectorの編集を、同じ履歴として混在させない
	ui_.BeginDisabled(gizmo_.IsUsing() || gizmoEditActive_ ||
	                  sceneViewPanel_.IsSelecting());
	inspectorPanel_.Draw(ui_, scene, selection_.GetActiveId(), history,
	                     clipboard_.GetTransform() != nullptr, &models_,
	                     &assets_, clipboard_.GetCollider() != nullptr,
	                     clipboard_.GetCameraLens() != nullptr,
	                     clipboard_.GetCharacterSettings() != nullptr,
	                     clipboard_.GetFollowSettings() != nullptr,
	                     clipboard_.GetRespawnSettings() != nullptr,
	                     selection_.GetIds(),
	                     clipboard_.GetTextureUVs() != nullptr);
	const auto &objectRequest = inspectorPanel_.GetObjectRequest();
	const bool hasObjectRequest =
	    objectRequest.action != InspectorPanel::ObjectAction::NONE;
	const auto &modelRequest = inspectorPanel_.GetModelRequest();
	const auto &visibilityRequest = inspectorPanel_.GetVisibilityRequest();
	const auto colliderFitRequest = inspectorPanel_.GetColliderFitRequest();
	ui_.EndDisabled();

	if (!wasBusy && !inspectorPanel_.GetAssetRevealPath().empty()) {
		assetsPanel_.RequestReveal(inspectorPanel_.GetAssetRevealPath());
	}

	const bool canSetTexture =
	    HasSelection() && !gizmo_.IsUsing() && !gizmoEditActive_ &&
	    !sceneViewPanel_.IsSelecting() &&
	    std::ranges::all_of(selection_.GetIds(), [&](const auto id) {
		    const auto *object = scene.FindObject(id);
		    return object && object->meshRenderer &&
		           object->meshRenderer->IsValid();
	    });
	const auto textureSlots = GetTextureSlotChoices();
	const auto requestedModelPath = assetsPanel_.Draw(
	    ui_, assets_, canSetTexture,
	    !gizmo_.IsUsing() && !gizmoEditActive_ &&
	        !sceneViewPanel_.IsSelecting(),
	    HasSelection() &&
	        std::ranges::all_of(selection_.GetIds(),
	                            [&](const auto id) {
		                            const auto *object = scene.FindObject(id);
		                            return object && object->mesh &&
		                                   object->mesh->IsValid();
	                            }),
	    textureSlots);
	if (!wasBusy && !assetsPanel_.GetTexturePreviewRequest().empty()) {
		texturePreviewPanel_.Open(assetsPanel_.GetTexturePreviewRequest());
	}

	texturePreviewPanel_.Draw(ui_);
	const auto requestedTexture =
	    assetsPanel_.IsTextureClearRequested()
	        ? std::filesystem::path{}
	        : assetsPanel_.GetTextureSelectionRequest();
	const auto requestedSceneAppend = assetsPanel_.GetSceneAppendRequest();
	const auto requestedInstances = assetsPanel_.GetInstanceSelectionRequest();
	const auto requestedModelReload = assetsPanel_.GetModelReloadRequest();
	const auto requestedMeshReplacement =
	    assetsPanel_.GetMeshReplacementRequest();
	const auto requestedTextureReload = assetsPanel_.GetTextureReloadRequest();
	const bool textureAssignment =
	    assetsPanel_.IsTextureClearRequested() || !requestedTexture.empty();
	const auto requestedTextureUsers =
	    assetsPanel_.GetTextureUsageSelectionRequest();

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
	const auto &sceneDrop = sceneViewPanel_.GetSceneDrop();
	const bool hasAssetDrop =
	    !modelDrop.path.empty() || !sceneDrop.path.empty();

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
	measurementsPanel_.Draw(ui_, scene, selection_.GetIds(), showMeasurements_);
	performancePanel_.Draw(
	    ui_, scene, selection_,
	    performanceTarget ? performanceTarget->GetWidth() : 0,
	    performanceTarget ? performanceTarget->GetHeight() : 0);

	if (!gizmoEditActive_ && !ImGui::IsAnyItemActive()) {
		history.CommitEdit();
	}

	if (!wasBusy) {
		if (!requestedSceneAppend.empty() && !IsBusy() &&
		    action == EditorAction::NONE &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			AppendSceneObjects(requestedSceneAppend);
		}

		const auto &requestedScene = assetsPanel_.GetSceneOpenRequest();
		if (!requestedScene.empty() && !IsBusy() &&
		    action == EditorAction::NONE &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			// 保存先の選択や確認中にAssetsの選択が変わっても、開く対象は保持する。
			pendingOpenPath_ = requestedScene;
			action = EditorAction::OPEN_PATH;
		}

		const auto &requestedSettings = assetsPanel_.GetGameSettingsRequest();
		if (!requestedSettings.empty() && !IsBusy() &&
		    action == EditorAction::NONE &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			pendingOpenPath_ = requestedSettings;
			action = EditorAction::LOAD_GAME_SETTINGS;
		}

		if (assetsPanel_.IsFolderCreateRequested() && !IsBusy() &&
		    action == EditorAction::NONE &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			try {
				FinishEdit();
				assetFolderDialog_.Show(assets_.GetRootDirectory(),
				                        assetsPanel_.GetOpenDirectory());
				stage_ = DialogStage::CREATE_ASSET_FOLDER;
			} catch (const std::exception &error) {
				Logger::Error(std::format("EditorScene::CreateAssetFolder: {}",
				                          error.what()));
				ShowError(
				    "Could not open folder creation. See the log for details.");
			}
		}

		if (assetsPanel_.IsModelImportRequested() && !IsBusy() &&
		    action == EditorAction::NONE &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			try {
				FinishEdit();
				modelImportDialog_.Show(assets_,
				                        assetsPanel_.GetOpenDirectory());
				stage_ = DialogStage::IMPORT_MODEL;
			} catch (const std::exception &exception) {
				Logger::Error(std::format("EditorScene::ImportModel: {}",
				                          exception.what()));
				ShowError(
				    "Could not open model import. See the log for details.");
			}
		}

		if (assetsPanel_.IsTextureImportRequested() && !IsBusy() &&
		    action == EditorAction::NONE &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			try {
				FinishEdit();
				textureImportDialog_.Show(assets_,
				                          assetsPanel_.GetOpenDirectory());
				stage_ = DialogStage::IMPORT_TEXTURE;
			} catch (const std::exception &error) {
				Logger::Error(std::format("EditorScene::ImportTexture: {}",
				                          error.what()));
				ShowError(
				    "Could not open image import. See the log for details.");
			}
		}

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
				case HierarchyPanel::Action::PASTE_RESPAWN_SETTINGS:
					PasteRespawnSettingsToSelection();
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
				case HierarchyPanel::Action::PLACE_ON_WORK_PLANE:
					PlaceSelectionOnWorkPlane();
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
				case HierarchyPanel::Action::ALIGN_COLLIDER_MIN_X:
					AlignSelectedColliderBounds(AlignmentAxis::X, false);
					break;
				case HierarchyPanel::Action::ALIGN_COLLIDER_MAX_X:
					AlignSelectedColliderBounds(AlignmentAxis::X, true);
					break;
				case HierarchyPanel::Action::ALIGN_COLLIDER_MIN_Y:
					AlignSelectedColliderBounds(AlignmentAxis::Y, false);
					break;
				case HierarchyPanel::Action::ALIGN_COLLIDER_MAX_Y:
					AlignSelectedColliderBounds(AlignmentAxis::Y, true);
					break;
				case HierarchyPanel::Action::ALIGN_COLLIDER_MIN_Z:
					AlignSelectedColliderBounds(AlignmentAxis::Z, false);
					break;
				case HierarchyPanel::Action::ALIGN_COLLIDER_MAX_Z:
					AlignSelectedColliderBounds(AlignmentAxis::Z, true);
					break;
				case HierarchyPanel::Action::DISTRIBUTE_COLLIDER_X:
					DistributeSelectedColliderGaps(AlignmentAxis::X);
					break;
				case HierarchyPanel::Action::DISTRIBUTE_COLLIDER_Y:
					DistributeSelectedColliderGaps(AlignmentAxis::Y);
					break;
				case HierarchyPanel::Action::DISTRIBUTE_COLLIDER_Z:
					DistributeSelectedColliderGaps(AlignmentAxis::Z);
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
		    !hasAssetDrop && modelRequest.objectId == 0 &&
		    visibilityRequest.objectId == 0 && !hasObjectRequest &&
		    !historyRequest &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			SelectModelInstances(requestedInstances);
		}

		if (!IsBusy() && action == EditorAction::NONE &&
		    !requestedMeshReplacement.empty() &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			ReplaceSelectedMeshes(requestedMeshReplacement);
		}

		if (!IsBusy() && action == EditorAction::NONE &&
		    !requestedModelReload.empty() &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			try {
				FinishEdit();
				if (!models_.Reload(requestedModelReload)) {
					ShowError("Could not reload the model. Its previous valid "
					          "data was kept. See the log for details.");
				}
			} catch (const std::exception &exception) {
				Logger::Error(std::format("EditorScene::ReloadModel: {}",
				                          exception.what()));
				ShowError("Could not reload the model. Its previous valid data "
				          "was kept. See the log for details.");
			}
		}

		if (!IsBusy() && action == EditorAction::NONE &&
		    !requestedTextureReload.empty() &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			try {
				FinishEdit();
				if (!materials_.ReloadTexture(requestedTextureReload)) {
					ShowError(
					    "Could not reload the texture. Its previous valid "
					    "data was kept. See the log for details.");
				}
			} catch (const std::exception &exception) {
				Logger::Error(std::format("EditorScene::ReloadTexture: {}",
				                          exception.what()));
				ShowError(
				    "Could not reload the texture. Its previous valid data "
				    "was kept. See the log for details.");
			}
		}

		if (!IsBusy() && action == EditorAction::NONE && textureAssignment &&
		    !hasCheckRequest && requestedTextureUsers.empty() &&
		    requestedSceneAppend.empty() && requestedInstances.empty() &&
		    requestedModelPath.empty() && !hasAssetDrop &&
		    modelRequest.objectId == 0 && visibilityRequest.objectId == 0 &&
		    colliderFitRequest == 0 && !hasObjectRequest && !historyRequest &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			AssignTextureToSelection(requestedTexture,
			                         assetsPanel_.GetTextureSlot());
		}

		if (!IsBusy() && action == EditorAction::NONE &&
		    !requestedTextureUsers.empty() && requestedSceneAppend.empty() &&
		    requestedInstances.empty() && requestedModelPath.empty() &&
		    !hasAssetDrop && modelRequest.objectId == 0 &&
		    visibilityRequest.objectId == 0 && colliderFitRequest == 0 &&
		    !hasObjectRequest && !historyRequest &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			SelectTextureUsers(requestedTextureUsers);
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

		if (!IsBusy()) {
			if (historyRequest && action == EditorAction::NONE &&
			    colliderFitRequest == 0 && requestedModelPath.empty() &&
			    requestedInstances.empty() && !hasAssetDrop &&
			    modelRequest.objectId == 0 && visibilityRequest.objectId == 0 &&
			    !hasObjectRequest && !textureAssignment &&
			    requestedModelReload.empty() &&
			    requestedMeshReplacement.empty() &&
			    requestedTextureReload.empty() && !hasCheckRequest &&
			    requestedTextureUsers.empty() && requestedSceneAppend.empty() &&
			    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
				RestoreHistory(*historyRequest);
			}

			if (action == EditorAction::NONE && colliderFitRequest == 0 &&
			    requestedInstances.empty() && !hasAssetDrop &&
			    modelRequest.objectId == 0 && visibilityRequest.objectId == 0 &&
			    !hasObjectRequest && !textureAssignment &&
			    requestedModelReload.empty() &&
			    requestedMeshReplacement.empty() &&
			    requestedTextureReload.empty() && !hasCheckRequest &&
			    requestedTextureUsers.empty() && requestedSceneAppend.empty() &&
			    !historyRequest) {
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
	    hasCheckRequest || textureAssignment ||
	    !requestedMeshReplacement.empty() || !requestedModelReload.empty() ||
	    !requestedTextureReload.empty() || !requestedModelPath.empty() ||
	    !requestedInstances.empty() || !requestedTextureUsers.empty() ||
	    !requestedSceneAppend.empty() || hasAssetDrop ||
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

		if (windowFocusRequest_) {
			ui_.FocusWindow(windowFocusRequest_);
		}

		return playMode_.UpdatePreview(scene, cameraController_.GetPose());
	}

	if (!renderer_.SetCameraPose(cameraController_.GetPose())) {
		Logger::Error("EditorScene: カメラの姿勢が無効です。");
		return false;
	}

	if (!renderer_.SetCameraSettings(sceneProjection_) ||
	    !renderer_.SetLighting(exportConfig_.lighting) ||
	    !renderer_.Update(target->GetWidth(), target->GetHeight())) {
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

		if (!renderer_.SetCameraSettings(sceneProjection_) ||
		    !renderer_.SetLighting(exportConfig_.lighting) ||
		    !renderer_.Update(target->GetWidth(), target->GetHeight())) {
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
	    hasAssetDrop) {
		Ray ray;
		const auto &drop = sceneDrop.path.empty() ? modelDrop : sceneDrop;
		if (!renderer_.CreatePickingRay(drop.imagePosition.x,
		                                drop.imagePosition.y, ray)) {
			ShowError("Could not determine the asset placement position.");
		} else {
			const bool placed = sceneDrop.path.empty()
			                        ? PlaceDroppedModel(drop.path, ray)
			                        : PlaceDroppedScene(drop.path, ray);
			if (placed &&
			    !renderer_.Update(target->GetWidth(), target->GetHeight())) {
				return false;
			}
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

	if (showJumpGuide_ &&
	    !jumpPreview_.Update(renderer_, scene, selection_.GetActiveId())) {
		Logger::Error("EditorScene: ジャンプ軌跡を登録できませんでした。");
		return false;
	}

	if (showMeasurements_ && !SelectionMeasurement::AppendLine(
	                             renderer_, scene, selection_.GetIds())) {
		return false;
	}

	if (stage_ == DialogStage::OBJECT_ARRAY &&
	    objectArrayDialog_.CanPreview() &&
	    !ColliderArrayPreview{}.Update(renderer_, scene,
	                                   objectArrayDialog_.GetSourceIds(),
	                                   objectArrayDialog_.GetSettings())) {
		Logger::Error(
		    "EditorScene: 等間隔複製の下書きを表示できませんでした。");
		return false;
	}

	AxisAlignedBox boxDraft;
	if (stage_ == DialogStage::BOX_CREATION &&
	    boxCreationDialog_.GetPreviewBounds(boxDraft)) {
		ColliderBounds::Lines lines;
		if (!colliderBounds_.BuildLines(boxDraft, lines)) {
			return false;
		}

		for (auto &line : lines) {
			line.color = {0.2f, 0.85f, 1.0f, 1.0f};
			if (!renderer_.AddDebugLine(line)) {
				return false;
			}
		}
	}

	// 全ての線を登録した後、一度だけ頂点バッファへ転送する
	if (!renderer_.UpdateDebugLines()) {
		Logger::Error("EditorScene: 線の頂点バッファを更新できませんでした。");
		return false;
	}

	if (windowFocusRequest_) {
		ui_.FocusWindow(windowFocusRequest_);
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
	if (closeApproved_ || !document_.IsModified()) {
		ClearRecovery();
	} else {
		RecordRecovery();
	}
	recovery_.Release();

	if (canSavePreferences_) {
		EditorPreferences current;
		current.showGrid = showGrid_;
		current.showSelectionBounds = showSelectionBounds_;
		current.showCameraGuides = showCameraGuides_;
		current.showRespawnGuides = showRespawnGuides_;
		current.showJumpGuide = showJumpGuide_;
		current.showMotionTrail = showMotionTrail_;
		current.maximizeGameOnPlay = playMode_.GetMaximizeOnPlay();
		current.showObjectMarkers = showObjectMarkers_;
		current.showMeasurements = showMeasurements_;
		current.colliderDisplay = colliderDisplay_;
		current.operation = gizmo_.GetOperation();
		current.space = gizmo_.GetSpace();
		current.pivotMode = gizmo_.GetPivotMode();
		current.snapping = gizmo_.IsSnapping();
		current.snap = gizmo_.GetSnapSettings();
		current.camera = cameraController_.GetSettings();
		current.sceneProjection = sceneProjection_;
		current.cameraBookmarks = cameraBookmarks_;
		current.grid = sceneGrid_.GetSettings();
		current.gameExport = exportConfig_;
		current.assets = assetSources_;
		const auto recent = recentScenes_.GetPaths();
		current.recentScenes.assign(recent.begin(), recent.end());

		if (!(current == preferences_) && current.Save()) {
			preferences_ = current;
		}
	}

	texturePreviewPanel_.Clear();
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
	pendingPlayCamera_ = 0;
	playMode_.SetPreviewCamera(0);
	inspectorPanel_.Clear();

	stage_ = DialogStage::IDLE;
	pendingAction_ = EditorAction::NONE;
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

	return std::format("{}{} - Editor", name,
	                   document_.IsModified() ? "*" : "");
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

		if (ui_.MenuItem("Import Game Scene...", nullptr, enabled)) {
			action = EditorAction::IMPORT_SCENE;
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
					pendingOpenPath_ = path;
					action = EditorAction::OPEN_PATH;
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

		if (ui_.MenuItem("Save Selection As...", nullptr,
		                 enabled && HasSelection() && !gizmo_.IsUsing() &&
		                     !gizmoEditActive_ &&
		                     !sceneViewPanel_.IsSelecting())) {
			action = EditorAction::SAVE_SELECTION;
		}

		if (ui_.MenuItem("Recover Scene...", nullptr, enabled)) {
			action = EditorAction::RECOVER_SCENE;
		}

		if (ui_.MenuItem("Assets Folder...", nullptr, enabled)) {
			action = EditorAction::ASSETS_FOLDER;
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

		if (ui_.MenuItem("Place Colliders on Work Plane", nullptr,
		                 objectEnabled &&
		                     ColliderPlacement::CanDrop(document_.GetScene(),
		                                                selection_.GetIds()))) {
			action = EditorAction::PLACE_ON_WORK_PLANE;
		}

		if (ui_.MenuItem("Paste Camera Lenses", nullptr,
		                 objectEnabled &&
		                     clipboard_.GetCameraLens() != nullptr)) {
			action = EditorAction::PASTE_CAMERA_LENSES;
		}

		if (ui_.MenuItem("Paste Respawn Settings", nullptr,
		                 objectEnabled &&
		                     clipboard_.GetRespawnSettings() != nullptr)) {
			action = EditorAction::PASTE_RESPAWN_SETTINGS;
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

		ui_.BeginDisabled(!objectEnabled || selection_.GetCount() < 2 ||
		                  !ColliderPlacement::CanDrop(document_.GetScene(),
		                                              selection_.GetIds()));

		if (ui_.BeginMenu("Align Collider Bounds")) {
			if (ui_.MenuItem("Min X to Active", nullptr, true)) {
				action = EditorAction::ALIGN_COLLIDER_MIN_X;
			}

			if (ui_.MenuItem("Max X to Active", nullptr, true)) {
				action = EditorAction::ALIGN_COLLIDER_MAX_X;
			}

			if (ui_.MenuItem("Min Y to Active", nullptr, true)) {
				action = EditorAction::ALIGN_COLLIDER_MIN_Y;
			}

			if (ui_.MenuItem("Max Y to Active", nullptr, true)) {
				action = EditorAction::ALIGN_COLLIDER_MAX_Y;
			}

			if (ui_.MenuItem("Min Z to Active", nullptr, true)) {
				action = EditorAction::ALIGN_COLLIDER_MIN_Z;
			}

			if (ui_.MenuItem("Max Z to Active", nullptr, true)) {
				action = EditorAction::ALIGN_COLLIDER_MAX_Z;
			}

			ui_.EndMenu();
		}

		ui_.EndDisabled();

		ui_.BeginDisabled(!objectEnabled || selection_.GetCount() < 3 ||
		                  !ColliderPlacement::CanDrop(document_.GetScene(),
		                                              selection_.GetIds()));

		if (ui_.BeginMenu("Distribute Collider Gaps")) {
			if (ui_.MenuItem("Equal Gaps X", nullptr, true)) {
				action = EditorAction::DISTRIBUTE_COLLIDER_X;
			}

			if (ui_.MenuItem("Equal Gaps Y", nullptr, true)) {
				action = EditorAction::DISTRIBUTE_COLLIDER_Y;
			}

			if (ui_.MenuItem("Equal Gaps Z", nullptr, true)) {
				action = EditorAction::DISTRIBUTE_COLLIDER_Z;
			}

			ui_.EndMenu();
		}

		ui_.EndDisabled();

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

		if (ui_.MenuItem("Box with Size...", nullptr, enabled)) {
			action = EditorAction::CREATE_SIZED_BOX;
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
			ShowSceneView();
		}

		if (ui_.MenuItem("Frame All", "Shift+F", enabled)) {
			action = EditorAction::FRAME_ALL;
			ShowSceneView();
		}

		if (ImGui::MenuItem("Follow Active in Play", nullptr,
		                    &followPlaySelection_,
		                    enabled && playMode_.IsPlaying() &&
		                        playSelection_.GetActiveId() != 0)) {
			followedPlayObject_ = 0;
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

		if (ui_.BeginMenu("Camera Bookmarks")) {
			for (std::size_t i = 0; i < EditorCameraBookmarks::kSlotCount;
			     ++i) {
				const auto label = std::format("View {}", i + 1);
				if (ui_.BeginMenu(label.c_str())) {
					const bool saved = cameraBookmarks_.Get(i) != nullptr;
					if (ui_.MenuItem("Save Current View", nullptr, enabled)) {
						cameraBookmarkSlot_ = i;
						action = EditorAction::SAVE_CAMERA_BOOKMARK;
					}
					if (ui_.MenuItem("Restore View", nullptr,
					                 enabled && saved)) {
						cameraBookmarkSlot_ = i;
						action = EditorAction::RESTORE_CAMERA_BOOKMARK;
					}
					if (ui_.MenuItem("Clear View", nullptr, enabled && saved)) {
						cameraBookmarkSlot_ = i;
						action = EditorAction::CLEAR_CAMERA_BOOKMARK;
					}
					ui_.EndMenu();
				}
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
			playMode_.RestoreLayout();
			gridSettingsRequested_ = true;
		}

		ImGui::MenuItem("Selection Bounds", nullptr, &showSelectionBounds_,
		                !IsBusy());
		ImGui::MenuItem("Camera Guides", nullptr, &showCameraGuides_,
		                !IsBusy());
		ImGui::MenuItem("Respawn Guides", nullptr, &showRespawnGuides_,
		                !IsBusy());
		ImGui::MenuItem("Character Jump Guide", nullptr, &showJumpGuide_,
		                !IsBusy());
		ImGui::MenuItem("Character Motion Trail", nullptr, &showMotionTrail_,
		                !IsBusy());

		if (ui_.MenuItem("Frame Motion Trail", nullptr,
		                 enabled && playMode_.IsPlaying() && showMotionTrail_ &&
		                     motionTrail_.GetLineCount() != 0)) {
			playFrameRequest_ = FrameRequest::MOTION_TRAIL;
			ShowSceneView();
		}

		if (ui_.MenuItem("Clear Motion Trail", nullptr,
		                 enabled && playMode_.IsPlaying() &&
		                     motionTrail_.GetLineCount() != 0)) {
			motionTrail_.Clear();
		}

		ImGui::MenuItem("Selection Measurements", nullptr, &showMeasurements_,
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
			playMode_.RestoreLayout();
			resetDockLayout_ = true;
		}

		if (ui_.MenuItem("History...", nullptr, !IsBusy())) {
			playMode_.RestoreLayout();
			historyPanel_.Show();
			windowFocusRequest_ = "History";
		}

		if (ui_.MenuItem("Console...", nullptr, !IsBusy())) {
			playMode_.RestoreLayout();
			consolePanel_.Show();
			windowFocusRequest_ = "Console";
		}

		if (ui_.MenuItem("Scene Check...", nullptr,
		                 !IsBusy() && !playMode_.IsPlaying())) {
			sceneCheckPanel_.Show();
		}

		if (ui_.MenuItem("Performance...", nullptr, !IsBusy())) {
			playMode_.RestoreLayout();
			performancePanel_.Show();
			windowFocusRequest_ = "Performance";
		}

		if (ui_.MenuItem("Play State...", nullptr,
		                 stage_ == DialogStage::IDLE && !exitQueued_)) {
			playMode_.RestoreLayout();
			playStatePanel_.Show();
			windowFocusRequest_ = "Play State";
		}

		ui_.EndMenu();
	}

	if (ui_.BeginMenu("Help")) {
		if (ui_.MenuItem("Controls...", nullptr, !IsBusy())) {
			playMode_.RestoreLayout();
			controlsPanel_.Show();
			windowFocusRequest_ = "Editor Controls";
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
	changed |= ImGui::DragFloat(
	    "Height", &settings.height, 0.01f, -SceneGrid::kMaximumHeight,
	    SceneGrid::kMaximumHeight, "%.3f", ImGuiSliderFlags_AlwaysClamp);
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

	ImGui::SeparatorText("Scene Projection");
	auto projection = sceneProjection_;
	bool projectionChanged = ImGui::DragFloat(
	    "Scene Vertical FOV", &projection.verticalFovDegrees, 0.1f, 1.0f,
	    179.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	projectionChanged |=
	    ImGui::DragFloat("Scene Near Clip", &projection.nearClip, 0.01f, 0.001f,
	                     99999.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	projectionChanged |=
	    ImGui::DragFloat("Scene Far Clip", &projection.farClip, 1.0f, 0.002f,
	                     100000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

	if (ImGui::Button("Reset Scene Projection")) {
		projection = CameraSettings{};
		projectionChanged = true;
	}

	if (projectionChanged && projection.IsValid()) {
		sceneProjection_ = projection;
	}

	ImGui::EndDisabled();
	ImGui::EndPopup();
}

EditorScene::EditorAction
EditorScene::ReadShortcut(bool objectPanelFocused) const {
	// 文字入力中の操作は、ImGuiの入力欄へ任せる
	if (IsBusy() || ImGui::GetIO().AppFocusLost || ui_.IsTextInputActive() ||
	    gizmo_.IsUsing() || sceneViewPanel_.IsSelecting() ||
	    document_.GetHistory().IsEditing()) {
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
	    action == EditorAction::NONE ||
	    ((action == EditorAction::IMPORT_SCENE ||
	      action == EditorAction::ASSETS_FOLDER ||
	      action == EditorAction::OPEN_PATH ||
	      action == EditorAction::LOAD_GAME_SETTINGS ||
	      action == EditorAction::RECOVER_SCENE ||
	      action == EditorAction::RESTORE_RECOVERY) &&
	     (playMode_.IsPlaying() || IsBusy()))) {
		return;
	}

	if (action == EditorAction::SAVE_SELECTION &&
	    (playMode_.IsPlaying() || IsBusy() || !HasSelection() ||
	     gizmo_.IsUsing() || gizmoEditActive_ ||
	     sceneViewPanel_.IsSelecting())) {
		return;
	}

	if (action == EditorAction::CREATE_SIZED_BOX &&
	    (playMode_.IsPlaying() || gizmo_.IsUsing() || gizmoEditActive_ ||
	     sceneViewPanel_.IsSelecting())) {
		return;
	}

	if ((action == EditorAction::SAVE_CAMERA_BOOKMARK ||
	     action == EditorAction::RESTORE_CAMERA_BOOKMARK ||
	     action == EditorAction::CLEAR_CAMERA_BOOKMARK) &&
	    (playMode_.IsPlaying() || gizmo_.IsUsing() || gizmoEditActive_ ||
	     sceneViewPanel_.IsSelecting())) {
		return;
	}

	// 保存やUndoを実行する前に、編集中の値を確定する
	FinishEdit();

	switch (action) {
	case EditorAction::RECOVER_SCENE:
		ShowRecovery();
		return;

	case EditorAction::SAVE_SELECTION:
		StartSaveSelection();
		return;

	case EditorAction::ASSETS_FOLDER:
		try {
			fileDialog_.Show(FileDialogMode::SELECT_DIRECTORY,
			                 assets_.GetRootDirectory());
			stage_ = DialogStage::SELECT_ASSET_FOLDER;
		} catch (const std::exception &error) {
			Logger::Error(
			    std::format("EditorScene::AssetsFolder: {}", error.what()));
			ShowError(
			    "Could not select an Assets folder. See the log for details.");
		}
		return;

	case EditorAction::GAME_SETTINGS:
	case EditorAction::LOAD_GAME_SETTINGS:
		gameSettingsDialog_.Show(exportConfig_,
		                         assets_.GetRootDirectory() /
		                             assetsPanel_.GetOpenDirectory(),
		                         action == EditorAction::LOAD_GAME_SETTINGS
		                             ? pendingOpenPath_
		                             : std::filesystem::path{});
		stage_ = DialogStage::GAME_SETTINGS;
		return;
	case EditorAction::EXPORT_GAME:
		pendingAction_ = action;
		try {
			ExecutePendingAction();
		} catch (const std::exception &error) {
			Logger::Error(
			    std::format("EditorScene::PrepareExport: {}", error.what()));
			ShowError(
			    "Could not prepare game export. See the log for details.");
		}
		return;

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
	case EditorAction::SAVE_CAMERA_BOOKMARK:
		if (!cameraBookmarks_.Set(cameraBookmarkSlot_,
		                          cameraController_.GetViewState(),
		                          sceneProjection_)) {
			ShowError("Could not save this Scene camera view.");
		}
		return;
	case EditorAction::RESTORE_CAMERA_BOOKMARK:
		if (const auto *state = cameraBookmarks_.Get(cameraBookmarkSlot_)) {
			const auto *projection =
			    cameraBookmarks_.GetProjection(cameraBookmarkSlot_);
			if ((!projection || projection->IsValid()) &&
			    cameraController_.SetViewState(*state)) {
				if (projection) {
					sceneProjection_ = *projection;
				}
				frameRequest_ = FrameRequest::NONE;
				ShowSceneView();
			} else {
				ShowError("Could not restore this Scene camera view.");
			}
		}
		return;
	case EditorAction::CLEAR_CAMERA_BOOKMARK:
		cameraBookmarks_.Clear(cameraBookmarkSlot_);
		return;
	case EditorAction::RESET_CAMERA:
		cameraController_.Reset();
		frameRequest_ = FrameRequest::NONE;
		ShowSceneView();
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

	case EditorAction::PLACE_ON_WORK_PLANE:
		PlaceSelectionOnWorkPlane();
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
	case EditorAction::ALIGN_COLLIDER_MIN_X:
		AlignSelectedColliderBounds(AlignmentAxis::X, false);
		return;
	case EditorAction::ALIGN_COLLIDER_MAX_X:
		AlignSelectedColliderBounds(AlignmentAxis::X, true);
		return;
	case EditorAction::ALIGN_COLLIDER_MIN_Y:
		AlignSelectedColliderBounds(AlignmentAxis::Y, false);
		return;
	case EditorAction::ALIGN_COLLIDER_MAX_Y:
		AlignSelectedColliderBounds(AlignmentAxis::Y, true);
		return;
	case EditorAction::ALIGN_COLLIDER_MIN_Z:
		AlignSelectedColliderBounds(AlignmentAxis::Z, false);
		return;
	case EditorAction::ALIGN_COLLIDER_MAX_Z:
		AlignSelectedColliderBounds(AlignmentAxis::Z, true);
		return;
	case EditorAction::DISTRIBUTE_COLLIDER_X:
		DistributeSelectedColliderGaps(AlignmentAxis::X);
		return;
	case EditorAction::DISTRIBUTE_COLLIDER_Y:
		DistributeSelectedColliderGaps(AlignmentAxis::Y);
		return;
	case EditorAction::DISTRIBUTE_COLLIDER_Z:
		DistributeSelectedColliderGaps(AlignmentAxis::Z);
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
	case EditorAction::OPEN_PATH:
	case EditorAction::RESTORE_RECOVERY:
	case EditorAction::IMPORT_SCENE:
	case EditorAction::EXIT:
		// 保存確認が終わるまで、実際の操作は保留する
		pendingAction_ = action;

		ConfirmSceneChanges();

		return;

	case EditorAction::SAVE:
		pendingAction_ = EditorAction::NONE;
		StartSave(false);
		return;

	case EditorAction::SAVE_AS:
		pendingAction_ = EditorAction::NONE;
		StartSave(true);
		return;

	case EditorAction::RELOAD_SCENE:
		pendingAction_ = EditorAction::NONE;

		if (!document_.GetFilePath().empty()) {
			sceneReloadDialog_.Show(
			    PathUtils::ToUtf8(document_.GetFilePath().filename()));
			stage_ = DialogStage::CONFIRM_RELOAD;
		}

		return;

	case EditorAction::SAVE_COPY:
		pendingAction_ = EditorAction::NONE;
		StartSaveCopy();
		return;
	case EditorAction::CREATE_EMPTY:
		CreateObject(SceneObject{});
		return;
	case EditorAction::CREATE_BOX:
		CreateObject(PrimitiveObjects::CreateBox());
		return;
	case EditorAction::CREATE_SIZED_BOX: {
		try {
			BoxCollider bounds;
			if (!GetFittedCollider(PrimitiveObjects::CreateBox(), bounds)) {
				ShowError(
				    "Could not load the built-in Box. No object was created.");
				return;
			}
			boxCreationDialog_.Show(bounds, sceneGrid_.GetSettings().height);
			stage_ = DialogStage::BOX_CREATION;
			ShowSceneView();
		} catch (const std::exception &error) {
			ShowError(error.what());
		}
		return;
	}
	case EditorAction::CREATE_CAMERA:
		CreateCameraFromView();
		return;

	case EditorAction::SNAP_POSITIONS:
		SnapSelectedPositions();
		return;

	case EditorAction::PASTE_CAMERA_LENSES:
		PasteCameraLensesToSelection();
		return;

	case EditorAction::PASTE_RESPAWN_SETTINGS:
		PasteRespawnSettingsToSelection();
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
		options.assetDirectories = assetSources_.ForScene(document_.GetScene());
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

			ClearRecovery();
			gizmo_.Cancel();
			gizmoEditActive_ = false;
			selection_.Clear();
			cameraController_.Reset();
			hierarchyPanel_.Clear();
			pendingPlayCamera_ = 0;
			playMode_.SetPreviewCamera(0);
			inspectorPanel_.Clear();
		} catch (const std::exception &error) {
			Logger::Error(error.what());
			ShowError("Could not create the scene. See the log for details.");
		}

		return;
	}

	case EditorAction::IMPORT_SCENE:
		try {
			sceneImportDialog_.Show(assets_, assetsPanel_.GetOpenDirectory());
			stage_ = DialogStage::IMPORT_SCENE;
		} catch (const std::exception &error) {
			Logger::Error(
			    std::format("EditorScene::ImportScene: {}", error.what()));
			ShowError(
			    "Could not prepare scene import. See the log for details.");
		}
		return;

	case EditorAction::OPEN:
		fileDialog_.Show(FileDialogMode::OPEN, document_.GetFilePath().empty()
		                                           ? GetNewScenePath({})
		                                           : document_.GetFilePath());

		stage_ = DialogStage::OPEN_FILE;
		return;
	case EditorAction::OPEN_PATH:
		OpenScene(pendingOpenPath_);
		return;
	case EditorAction::RESTORE_RECOVERY:
		RestoreRecovery();
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
			ClearRecovery();
			assets_.Refresh();
			ExecutePendingAction();
		} else {
			ShowError(document_.GetError());
		}

		return;
	}

	const auto path = document_.GetFilePath().empty()
	                      ? GetNewScenePath(L"Untitled.scene.json")
	                      : document_.GetFilePath();
	fileDialog_.Show(FileDialogMode::SAVE, path);

	stage_ = DialogStage::SAVE_FILE;
}

void EditorScene::CreateCameraFromView() {
	const auto object = CameraObjects::CreateFromView(
	    cameraController_.GetPose(), sceneProjection_);

	if (!object) {
		ShowError("The Scene view camera pose or projection is invalid. No "
		          "Camera was created.");
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

void EditorScene::SelectTextureUsers(const std::filesystem::path &path) {
	FinishEdit();

	try {
		const auto count =
		    TextureUsage::SelectObjects(document_.GetScene(), path, selection_);
		hierarchyPanel_.RequestReveal(selection_.GetActiveId());

		if (count == 0) {
			Logger::Info("この材質を割り当てた物体はシーン内にありません。");
		}
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::SelectTextureUsers: {}", error.what()));
		ShowError("Could not select texture users. See the log for details.");
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
	ShowSceneView();
}

std::filesystem::path
EditorScene::GetNewScenePath(const std::filesystem::path &name) const {
	const auto &directory = assetSources_.GetDirectory();
	if (name.empty()) {
		return directory.is_absolute() ? directory : std::filesystem::path{};
	}
	return directory.is_absolute() ? directory / name : name;
}

void EditorScene::StartSaveCopy() {
	try {
		const auto &currentPath = document_.GetFilePath();
		auto copyPath = currentPath.empty()
		                    ? GetNewScenePath(L"Untitled.copy.scene.json")
		                    : currentPath.parent_path() /
		                          (currentPath.stem().native() + L".copy" +
		                           currentPath.extension().native());
		fileDialog_.Show(FileDialogMode::SAVE_COPY, copyPath);
		stage_ = DialogStage::SAVE_COPY_FILE;
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::StartSaveCopy: {}", error.what()));
		ShowError(
		    "Could not select a copy destination. See the log for details.");
	}
}

void EditorScene::StartSaveSelection() {
	try {
		if (!SceneObjects::Capture(document_.GetScene(), selection_.GetIds(),
		                           selectionSnapshot_)) {
			ShowError(
			    "Could not prepare the selected objects. No file was written.");
			return;
		}
		const auto folder = AssetFolders::Resolve(
		    assets_.GetRootDirectory(), assetsPanel_.GetOpenDirectory());
		fileDialog_.Show(FileDialogMode::SAVE_COPY,
		                 folder / L"Selection.scene.json");
		stage_ = DialogStage::SAVE_SELECTION_FILE;
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::SaveSelection: {}", error.what()));
		ShowError(
		    "Could not prepare a selection copy. See the log for details.");
	}
}

void EditorScene::AppendSceneObjects(const std::filesystem::path &path) {
	if (playMode_.IsPlaying() || IsBusy() || gizmo_.IsUsing() ||
	    gizmoEditActive_ || sceneViewPanel_.IsSelecting()) {
		return;
	}
	try {
		SceneDocument source;
		if (!source.Open(path)) {
			ShowError("Could not read objects from the scene. The current "
			          "scene was kept.");
			return;
		}
		auto ids =
		    SceneObjects::Append(source.GetScene(), document_.GetHistory());
		if (ids.empty()) {
			ShowError("The scene has no objects to add, or its settings are "
			          "invalid.");
			return;
		}
		selection_.Set(std::move(ids));
		hierarchyPanel_.RequestReveal(selection_.GetActiveId());
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::AppendSceneObjects: {}", error.what()));
		ShowError(
		    "Could not add objects from the scene. See the log for details.");
	}
}

bool EditorScene::PlaceDroppedScene(const std::filesystem::path &path,
                                    const Ray &ray) {
	if (playMode_.IsPlaying() || IsBusy() || gizmo_.IsUsing() ||
	    gizmoEditActive_ || sceneViewPanel_.IsSelecting()) {
		return false;
	}
	try {
		SceneDocument source;
		if (!source.Open(path) ||
		    !ScenePlacement::Place(
		        source.GetScene(), document_.GetScene(), ray, models_,
		        gizmo_.IsSnapping()
		            ? std::optional{gizmo_.GetSnapSettings().translation}
		            : std::nullopt,
		        sceneGrid_.GetSettings().height)) {
			ShowError("Could not position the scene objects. No objects were "
			          "added. See the log for details.");
			return false;
		}
		auto ids =
		    SceneObjects::Append(source.GetScene(), document_.GetHistory());
		if (ids.empty()) {
			ShowError(
			    "Could not add the positioned objects. No objects were added.");
			return false;
		}
		selection_.Set(std::move(ids));
		hierarchyPanel_.RequestReveal(selection_.GetActiveId());
		return true;
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::PlaceDroppedScene: {}", error.what()));
		ShowError("Could not place scene objects. See the log for details.");
		return false;
	}
}

void EditorScene::UpdateRecovery(float deltaTime) {
	if (!recovery_.IsReady()) {
		return;
	}
	if (!document_.IsModified()) {
		if (recoveryWasDirty_) {
			ClearRecovery();
		}
		recoveryWasDirty_ = false;
		recoveryElapsed_ = 0;
		return;
	}
	recoveryWasDirty_ = true;
	recoveryElapsed_ = (std::min)(SceneRecovery::kIntervalSeconds,
	                              recoveryElapsed_ + deltaTime);
	if (recoveryElapsed_ < SceneRecovery::kIntervalSeconds || IsBusy() ||
	    playMode_.IsPlaying() || document_.GetHistory().IsEditing() ||
	    gizmo_.IsUsing() || gizmoEditActive_ || sceneViewPanel_.IsSelecting() ||
	    ImGui::IsAnyItemActive()) {
		return;
	}
	RecordRecovery();
}

void EditorScene::RecordRecovery() {
	recoveryElapsed_ = 0;
	if (!recovery_.IsReady()) {
		return;
	}
	std::string error;
	if (!recovery_.Record(document_.GetScene(), document_.GetFilePath(),
	                      assetSources_, exportConfig_, error)) {
		Logger::Error("EditorScene::RecordRecovery: " + error);
		return;
	}
	if (!restoredRecoveryPath_.empty()) {
		if (!document_.GetFilePath().empty() &&
		    PathUtils::IsSameFile(restoredRecoveryPath_,
		                          document_.GetFilePath())) {
			restoredRecoveryPath_.clear();
		} else if (recovery_.Remove(restoredRecoveryPath_, error)) {
			restoredRecoveryPath_.clear();
		} else {
			Logger::Error("EditorScene::RemoveRecovery: " + error);
		}
	}
}

void EditorScene::ClearRecovery(
    const std::filesystem::path &protectedRecovery) {
	recoveryElapsed_ = 0;
	std::string error;
	if (!recovery_.Clear(document_.GetFilePath(), error)) {
		Logger::Error("EditorScene::ClearRecovery: " + error);
	}
	if (!restoredRecoveryPath_.empty()) {
		const auto &protectedCandidate =
		    pendingAction_ == EditorAction::RESTORE_RECOVERY
		        ? pendingRecoveryPath_
		        : protectedRecovery;
		if (!protectedCandidate.empty() &&
		    PathUtils::IsSameFile(restoredRecoveryPath_, protectedCandidate)) {
			restoredRecoveryPath_.clear();
		} else if (!document_.GetFilePath().empty() &&
		           PathUtils::IsSameFile(restoredRecoveryPath_,
		                                 document_.GetFilePath())) {
			restoredRecoveryPath_.clear();
		} else if (recovery_.Remove(restoredRecoveryPath_, error)) {
			restoredRecoveryPath_.clear();
		} else {
			Logger::Error("EditorScene::RemoveRecovery: " + error);
		}
	}
}

void EditorScene::ShowRecovery() {
	std::vector<SceneRecovery::Entry> entries;
	std::string error;
	recovery_.Enumerate(entries, error);
	recoveryDialog_.Show(std::move(entries), std::move(error));
	stage_ = DialogStage::RECOVERY;
}

void EditorScene::RestoreRecovery() {
	try {
		SceneRecovery::Snapshot snapshot;
		std::string error;
		if (!recovery_.Read(pendingRecoveryPath_, snapshot, error)) {
			ShowError(
			    "Could not restore the scene. The current scene was kept: " +
			    error);
			return;
		}
		AssetDatabase assets(snapshot.assets.GetDirectory());
		assets.Refresh();
		auto restoredPath = pendingRecoveryPath_;
		document_.GetScene().ReplaceObjects(snapshot.scene.GetObjects());
		document_.SetUntitled();
		ClearRecovery(pendingRecoveryPath_);
		assetSources_ = std::move(snapshot.assets);
		exportConfig_ = std::move(snapshot.game);
		assets_ = std::move(assets);
		restoredRecoveryPath_.swap(restoredPath);
		sceneCheckPanel_.Reset();
		clipboard_.DetachExternalReferences();
		frameRequest_ = FrameRequest::NONE;
		frameObject_ = 0;
		gizmo_.Cancel();
		gizmoEditActive_ = false;
		selection_.Clear();
		cameraController_.Reset();
		hierarchyPanel_.Clear();
		pendingPlayCamera_ = 0;
		playMode_.SetPreviewCamera(0);
		inspectorPanel_.Clear();
		assetsPanel_.Clear();
		stage_ = DialogStage::IDLE;
		RecordRecovery();
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::RestoreRecovery: {}", error.what()));
		ShowError("Could not restore the scene. See the log for details.");
	}
}

void EditorScene::UpdateDialogs() {
	switch (stage_) {
	case DialogStage::IDLE:
		return;

	case DialogStage::RECOVERY: {
		const auto result =
		    recoveryDialog_.Draw(ui_, recovery_, document_.GetFilePath());
		if (result == SceneRecoveryDialog::Result::CLOSED) {
			stage_ = DialogStage::IDLE;
		} else if (result == SceneRecoveryDialog::Result::RESTORE) {
			pendingRecoveryPath_ = recoveryDialog_.GetSelectedPath();
			stage_ = DialogStage::IDLE;
			StartAction(EditorAction::RESTORE_RECOVERY);
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
			stage_ = DialogStage::IDLE;
			break;

		case SaveChangesResult::NONE:
			break;
		}

		return;
	}

	case DialogStage::OPEN_FILE: {
		const auto result = fileDialog_.Draw(ui_);

		if (result == FileDialogResult::CANCELLED) {
			stage_ = DialogStage::IDLE;
			return;
		}

		if (result == FileDialogResult::ACCEPTED) {
			OpenScene(fileDialog_.GetSelectedPath());
		}

		return;
	}

	case DialogStage::SAVE_FILE: {
		const auto result = fileDialog_.Draw(ui_);

		if (result == FileDialogResult::CANCELLED) {
			// 保存先選択を取り消した場合は、NewやExitも取り消す
			pendingAction_ = EditorAction::NONE;
			stage_ = DialogStage::IDLE;
			return;
		}

		if (result == FileDialogResult::ACCEPTED) {
			if (document_.SaveAs(fileDialog_.GetSelectedPath())) {
				recentScenes_.Add(document_.GetFilePath());
				ClearRecovery();
				assets_.Refresh();
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

	case DialogStage::SAVE_SELECTION_FILE: {
		const auto result = fileDialog_.Draw(ui_);
		if (result == FileDialogResult::CANCELLED) {
			selectionSnapshot_.Clear();
			stage_ = DialogStage::IDLE;
		} else if (result == FileDialogResult::ACCEPTED) {
			if (document_.SaveCopy(fileDialog_.GetSelectedPath(),
			                       selectionSnapshot_)) {
				assets_.Refresh();
				assetsPanel_.RequestReveal(fileDialog_.GetSelectedPath());
				selectionSnapshot_.Clear();
				stage_ = DialogStage::IDLE;
			} else {
				ShowError(document_.GetError());
			}
		}
		return;
	}

	case DialogStage::SAVE_COPY_FILE: {
		const auto result = fileDialog_.Draw(ui_);

		if (result == FileDialogResult::CANCELLED) {
			stage_ = DialogStage::IDLE;
		} else if (result == FileDialogResult::ACCEPTED) {
			if (document_.SaveCopy(fileDialog_.GetSelectedPath())) {
				recentScenes_.Add(fileDialog_.GetSelectedPath());
				assets_.Refresh();
				stage_ = DialogStage::IDLE;
			} else {
				ShowError(document_.GetError());
			}
		}

		return;
	}

	case DialogStage::SELECT_ASSET_FOLDER: {
		const auto result = fileDialog_.Draw(ui_);
		if (result == FileDialogResult::CANCELLED) {
			stage_ = DialogStage::IDLE;
		} else if (result == FileDialogResult::ACCEPTED) {
			try {
				AssetDatabase candidate(fileDialog_.GetSelectedPath());
				if (!candidate.Refresh()) {
					throw std::runtime_error(candidate.GetError());
				}
				auto sources = assetSources_;
				sources.Select(candidate.GetRootDirectory());
				assets_ = std::move(candidate);
				assetSources_ = std::move(sources);
				assetsPanel_.Clear();
				assetsPanel_.RequestOpenDirectory({});
				stage_ = DialogStage::IDLE;
			} catch (const std::exception &error) {
				Logger::Error(
				    std::format("EditorScene::AssetsFolder: {}", error.what()));
				ShowError("The current Assets folder was kept: " +
				          std::string(error.what()));
			}
		}
		return;
	}

	case DialogStage::CREATE_ASSET_FOLDER: {
		const auto result = assetFolderDialog_.Draw(ui_);

		if (result == AssetFolderResult::ACCEPTED) {
			stage_ = DialogStage::IDLE;
			if (!assets_.Refresh()) {
				ShowError(
				    "The folder was created, but Assets could not refresh: " +
				    assets_.GetError());
			} else {
				assetsPanel_.RequestOpenDirectory(
				    assetFolderDialog_.GetCreatedDirectory());
			}
		} else if (result == AssetFolderResult::CANCELLED) {
			stage_ = DialogStage::IDLE;
		}
		return;
	}

	case DialogStage::IMPORT_SCENE: {
		const auto result = sceneImportDialog_.Draw(ui_, document_.GetScene());
		if (result == SceneImportResult::ACCEPTED) {
			if (const auto *imported =
			        sceneImportDialog_.GetImportedSettings()) {
				auto settings = *imported;
				// パッケージのシーン参照・予備カメラは現在のEditor設定へ持ち込まない。
				settings.scenePath = exportConfig_.scenePath;
				settings.fallbackCamera = exportConfig_.fallbackCamera;
				exportConfig_ = std::move(settings);
			}
			document_.SetUntitled();
			ClearRecovery();
			sceneCheckPanel_.Reset();
			clipboard_.DetachExternalReferences();
			frameRequest_ = FrameRequest::NONE;
			frameObject_ = 0;
			gizmo_.Cancel();
			gizmoEditActive_ = false;
			selection_.Clear();
			cameraController_.Reset();
			hierarchyPanel_.Clear();
			pendingPlayCamera_ = 0;
			playMode_.SetPreviewCamera(0);
			inspectorPanel_.Clear();
			stage_ = DialogStage::IDLE;
			if (!assets_.Refresh()) {
				ShowError(
				    "The scene was imported, but Assets could not refresh: " +
				    assets_.GetError());
			} else {
				assetsPanel_.RequestOpenDirectory(
				    sceneImportDialog_.GetCreatedDirectory());
			}
		} else if (result == SceneImportResult::CANCELLED) {
			stage_ = DialogStage::IDLE;
		}
		return;
	}

	case DialogStage::IMPORT_MODEL: {
		const auto result = modelImportDialog_.Draw(ui_, models_);
		if (result == ModelImportResult::ACCEPTED) {
			stage_ = DialogStage::IDLE;
			if (!assets_.Refresh()) {
				ShowError(
				    "The model was imported, but Assets could not refresh: " +
				    assets_.GetError());
			} else {
				assetsPanel_.RequestReveal(
				    modelImportDialog_.GetImportedPath());
			}
		} else if (result == ModelImportResult::CANCELLED) {
			stage_ = DialogStage::IDLE;
		}
		return;
	}

	case DialogStage::IMPORT_TEXTURE: {
		const auto result = textureImportDialog_.Draw(ui_);
		if (result == TextureImportResult::ACCEPTED) {
			stage_ = DialogStage::IDLE;
			if (!assets_.Refresh()) {
				ShowError(
				    "The image was imported, but Assets could not refresh: " +
				    assets_.GetError());
			} else {
				assetsPanel_.RequestReveal(
				    textureImportDialog_.GetImportedPath());
			}
		} else if (result == TextureImportResult::CANCELLED) {
			stage_ = DialogStage::IDLE;
		}
		return;
	}

	case DialogStage::BOX_CREATION: {
		const auto result = boxCreationDialog_.Draw(ui_);
		if (result == BoxCreationResult::FRAME_PREVIEW) {
			frameRequest_ = FrameRequest::DRAFT;
			return;
		}

		if (result == BoxCreationResult::NONE) {
			return;
		}
		if (frameRequest_ == FrameRequest::DRAFT) {
			frameRequest_ = FrameRequest::NONE;
		}
		stage_ = DialogStage::IDLE;
		if (result == BoxCreationResult::ACCEPTED) {
			try {
				BoxCollider bounds;
				SceneObject object;
				std::string error;
				if (!GetFittedCollider(PrimitiveObjects::CreateBox(), bounds)) {
					ShowError("Could not load the built-in Box. No object was "
					          "created.");
				} else if (!PrimitiveObjects::TryCreateBox(
				               boxCreationDialog_.GetSettings(), bounds, object,
				               error)) {
					ShowError(error);
				} else {
					CreateObject(std::move(object));
				}
			} catch (const std::exception &error) {
				ShowError(error.what());
			}
		}
		return;
	}

	case DialogStage::OBJECT_ARRAY: {
		const auto result = objectArrayDialog_.Draw(ui_, document_.GetScene());

		if (result == ObjectArrayResult::FRAME_PREVIEW) {
			frameRequest_ = FrameRequest::DRAFT;
			return;
		}

		if (result == ObjectArrayResult::NONE) {
			return;
		}
		if (frameRequest_ == FrameRequest::DRAFT) {
			frameRequest_ = FrameRequest::NONE;
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

	case DialogStage::EXPORT_RESULT:
		if (exportResultDialog_.Draw(ui_) == GameExportResultAction::CLOSE) {
			stage_ = DialogStage::IDLE;
		}
		return;

	case DialogStage::GAME_SETTINGS: {
		const auto result = gameSettingsDialog_.Draw(ui_);

		if (result == GameSettingsResult::ACCEPTED) {
			exportConfig_ = gameSettingsDialog_.GetConfig();
		}

		if (result != GameSettingsResult::NONE) {
			if (!gameSettingsDialog_.GetLastSavedPath().empty()) {
				assets_.Refresh();
			}
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
				exportResultDialog_.Show(
				    exportDialog_.GetOptions().destination);
				stage_ = DialogStage::EXPORT_RESULT;
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
	const bool opened = document_.Open(path);

	if (!opened) {
		ShowError("Could not open the scene. The current scene was kept. See "
		          "the log for details.");
		return;
	}

	ClearRecovery();
	sceneCheckPanel_.Reset();
	clipboard_.DetachExternalReferences();
	frameRequest_ = FrameRequest::NONE;
	frameObject_ = 0;
	FinishEdit();
	selection_.Clear();
	cameraController_.Reset();
	hierarchyPanel_.Clear();
	pendingPlayCamera_ = 0;
	playMode_.SetPreviewCamera(0);
	inspectorPanel_.Clear();
	recentScenes_.Add(document_.GetFilePath());
	stage_ = DialogStage::IDLE;
}

void EditorScene::ShowError(std::string message, SceneObjectId objectId) {
	errorMessage_ = std::move(message);
	errorObjectId_ = document_.GetScene().Contains(objectId) ? objectId : 0;
	errorOpenRequested_ = true;

	pendingAction_ = EditorAction::NONE;
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

		if (request.action ==
		    InspectorPanel::ObjectAction::SET_WORK_PLANE_FROM_COLLIDER) {
			AxisAlignedBox bounds;

			if (!object->collider ||
			    !BoxColliderUtils::GetWorldBounds(*object->collider,
			                                      object->transform, bounds)) {
				ShowError(
				    "Could not read a valid Collider top for the work plane.",
				    request.objectId);
				return;
			}

			auto settings = sceneGrid_.GetSettings();
			settings.height = bounds.maximum.y;

			if (!sceneGrid_.SetSettings(settings)) {
				ShowError("The Collider top is outside the supported "
				          "work-plane height range.",
				          request.objectId);
			}

			return;
		}

		if (request.action ==
		    InspectorPanel::ObjectAction::CLEAR_UNUSED_TEXTURES) {
			const auto model = object->HasMesh()
			                       ? models_.FindLoaded(object->mesh->modelPath)
			                       : ModelHandle{};

			if (!model || !object->meshRenderer ||
			    !object->meshRenderer->IsValid()) {
				ShowError("Load a valid Mesh and Mesh Renderer before clearing "
				          "unused image assignments.",
				          request.objectId);
				return;
			}

			auto edited = *object->meshRenderer;
			std::erase_if(edited.textures.slots, [&](const auto &binding) {
				return models_.GetMaterialMeshCount(model, binding.first) == 0;
			});

			if (!document_.GetHistory().SetMeshRenderer(request.objectId,
			                                            edited)) {
				ShowError("Could not clear unused image assignments.",
				          request.objectId);
			}

			return;
		}

		if (request.action == InspectorPanel::ObjectAction::COPY_UV) {
			if (!object->meshRenderer || !object->meshRenderer->IsValid() ||
			    !clipboard_.CopyTextureUVs(object->meshRenderer->uvScale,
			                               object->meshRenderer->uvOffset)) {
				ShowError("Could not copy valid UV settings from the inspected "
				          "object.");
			}

			return;
		}

		if (request.action ==
		    InspectorPanel::ObjectAction::PASTE_UV_TO_SELECTION) {
			const auto *copied = clipboard_.GetTextureUVs();

			if (!copied ||
			    !document_.GetHistory().SetTextureUVs(
			        selection_.GetIds(), copied->scale, copied->offset)) {
				ShowError("Could not paste UV settings. Copy valid UVs and "
				          "select objects with valid Mesh Renderers. No "
				          "selected object was changed.");
			}

			return;
		}

		if (request.action ==
		    InspectorPanel::ObjectAction::APPLY_UV_TO_SELECTION) {
			if (!object->meshRenderer || !object->meshRenderer->IsValid()) {
				ShowError("Could not read valid UV settings from the inspected "
				          "object.");
				return;
			}

			const auto scale = object->meshRenderer->uvScale;
			const auto offset = object->meshRenderer->uvOffset;

			if (!document_.GetHistory().SetTextureUVs(selection_.GetIds(),
			                                          scale, offset)) {
				ShowError(
				    "Could not apply UV settings. Every selected object must "
				    "have "
				    "a valid Mesh Renderer. No selected object was changed.");
			}

			return;
		}

		if (request.action == InspectorPanel::ObjectAction::PREVIEW_CAMERA) {
			if (!object->camera || !object->camera->IsValid() ||
			    !playMode_.SetPreviewCamera(request.objectId)) {
				ShowError("Choose a valid Camera while Play is stopped to "
				          "preview it in Game.",
				          request.objectId);
			}

			return;
		}

		if (request.action == InspectorPanel::ObjectAction::PLAY_FROM_CAMERA) {
			if (!playMode_.IsPlaying() && object->camera) {
				pendingPlayCamera_ = request.objectId;
			}

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

		if (request.action == InspectorPanel::ObjectAction::MOVE_TO_RESPAWN) {
			RespawnPreview::Geometry geometry;

			if (!object->respawn || object->respawn->useStartPosition ||
			    !respawnPreview_.BuildGeometry(document_.GetScene(),
			                                   request.objectId, geometry) ||
			    !geometry.canRespawn) {
				ShowError("Choose an explicit, valid and unobstructed Respawn "
				          "position before moving the object.",
				          request.objectId);
				return;
			}

			auto transform = object->transform;
			transform.translation = object->respawn->position;
			const std::array edits{
			    EditorHistory::TransformEdit{request.objectId, transform}};

			if (!document_.GetHistory().SetTransforms(edits)) {
				ShowError("Could not move the object to its Respawn position.",
				          request.objectId);
			}

			return;
		}

		if (request.action ==
		    InspectorPanel::ObjectAction::COPY_RESPAWN_SETTINGS) {
			if (!object->respawn ||
			    !clipboard_.CopyRespawnSettings(*object->respawn)) {
				ShowError("Could not copy invalid Respawn settings.");
			}

			return;
		}

		if (request.action ==
		    InspectorPanel::ObjectAction::PASTE_RESPAWN_SETTINGS) {
			const auto *copied = clipboard_.GetRespawnSettings();

			if (!copied ||
			    !document_.GetHistory().SetRespawn(request.objectId, *copied)) {
				ShowError("Could not paste Respawn settings. Copy valid "
				          "settings first.");
			}

			return;
		}

		if (request.action ==
		    InspectorPanel::ObjectAction::COPY_FOLLOW_SETTINGS) {
			if (!object->followCamera ||
			    !clipboard_.CopyFollowSettings(*object->followCamera)) {
				ShowError("Could not copy invalid Follow Camera settings.");
			}

			return;
		}

		if (request.action ==
		    InspectorPanel::ObjectAction::PASTE_FOLLOW_SETTINGS) {
			const auto *copied = clipboard_.GetFollowSettings();

			if (!copied) {
				ShowError("Copy valid Follow Camera settings before pasting.");
				return;
			}

			auto value = *copied;
			value.target =
			    object->followCamera ? object->followCamera->target : 0;

			if (!document_.GetHistory().SetFollowCamera(request.objectId,
			                                            value)) {
				ShowError("Could not paste Follow Camera settings.");
			}

			return;
		}

		if (request.action == InspectorPanel::ObjectAction::FRAME_FOLLOW_PATH) {
			std::array<DirectX::XMFLOAT3, 8> corners;
			if (!followCameraPreview_.GetFrameBounds(
			        document_.GetScene(), request.objectId, corners)) {
				ShowError("Could not frame the Follow Camera path. Check its "
				          "Camera, target and offsets.",
				          request.objectId);
				return;
			}
			frameObject_ = request.objectId;
			frameRequest_ = FrameRequest::FOLLOW_PATH;
			ShowSceneView();
			return;
		}

		if (request.action ==
		    InspectorPanel::ObjectAction::COPY_CHARACTER_SETTINGS) {
			if (!object->character ||
			    !clipboard_.CopyCharacterSettings(*object->character)) {
				ShowError(
				    "Could not copy invalid Character Controller settings.");
			}

			return;
		}

		if (request.action ==
		    InspectorPanel::ObjectAction::PASTE_CHARACTER_SETTINGS) {
			const auto *copied = clipboard_.GetCharacterSettings();

			if (!copied || !document_.GetHistory().SetCharacter(
			                   request.objectId, *copied)) {
				ShowError("Could not paste Character Controller settings. Copy "
				          "valid settings first.");
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

		if (request.action == InspectorPanel::ObjectAction::FRAME_JUMP) {
			std::array<DirectX::XMFLOAT3, 8> corners;

			if (!jumpPreview_.GetFrameBounds(document_.GetScene(),
			                                 request.objectId, corners)) {
				ShowError(
				    "Could not frame the jump. Check the Character Controller, "
				    "Collider and grounded starting position.");
				return;
			}

			frameObject_ = request.objectId;
			frameRequest_ = FrameRequest::JUMP;
			ShowSceneView();
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
			ShowSceneView();
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

		if (request.action ==
		    InspectorPanel::ObjectAction::SELECT_GROUND_COLLIDER) {
			CollisionWorld world;
			CharacterController controller;
			SceneObjectId ground = 0;

			if (!object->character || !object->collider ||
			    !world.Rebuild(document_.GetScene(), request.objectId) ||
			    !controller.Initialize(object->transform, *object->collider,
			                           *object->character, world) ||
			    !controller.FindGroundObject(world, ground)) {
				ShowError(
				    "Could not inspect the ground Collider. Check the "
				    "Character, Collider, Transform and obstacle settings.",
				    request.objectId);
				return;
			}

			if (ground == 0) {
				ShowError(
				    "No supporting Collider was found below this character.",
				    request.objectId);
				return;
			}

			selection_.SelectOnly(ground);
			hierarchyPanel_.RequestReveal(ground);
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
		               InspectorPanel::ObjectAction::ALIGN_CAMERA_VIEW ||
		           request.action ==
		               InspectorPanel::ObjectAction::MATCH_CAMERA_VIEW) {
			const auto projection =
			    request.action ==
			            InspectorPanel::ObjectAction::MATCH_CAMERA_VIEW
			        ? std::optional<CameraSettings>{sceneProjection_}
			        : std::nullopt;
			applied = document_.GetHistory().AlignCameraView(
			    request.objectId, cameraController_.GetPose(), projection);
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
			        InspectorPanel::ObjectAction::ALIGN_CAMERA_VIEW ||
			    request.action ==
			        InspectorPanel::ObjectAction::MATCH_CAMERA_VIEW) {
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

		// 全対象を確認してから、一括変更を履歴へ反映する
		for (const auto id : objectIds) {
			const auto *object = document_.GetScene().FindObject(id);

			if (!object) {
				ShowError("Could not find an object to change its visibility.");
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

bool EditorScene::ReplaceSelectedMeshes(
    const std::filesystem::path &modelPath) {
	try {
		FinishEdit();
		if (modelPath.empty() || !models_.Load(modelPath)) {
			ShowError(
			    "Could not load the model. The selected meshes were kept.");
			return false;
		}

		if (!document_.GetHistory().SetModelPaths(selection_.GetIds(),
		                                          modelPath)) {
			ShowError("Could not replace the meshes. Every selected object "
			          "must have a Mesh component.");
			return false;
		}

		return true;
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene::ReplaceSelectedMeshes: {}",
		                          error.what()));
		ShowError("Could not replace the meshes. See the log for details.");
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

		if (!ModelPlacement::GetSurface(ray, document_.GetScene(), surface,
		                                sceneGrid_.GetSettings().height)) {
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
	const auto excluded = runtimeScene
	                          ? std::span<const SceneObjectId>{}
	                          : hierarchyPanel_.GetScenePickingDisabledIds();

	if (request.action == Action::CLICK) {
		float modelDistance = (std::numeric_limits<float>::max)();
		SceneObjectId picked = renderer.Pick(
		    request.minimum.x, request.minimum.y, &modelDistance, excluded);
		Ray ray;

		if (showObjectMarkers_ &&
		    renderer.CreatePickingRay(request.minimum.x, request.minimum.y,
		                              ray)) {
			if (const auto marker = ObjectMarkers::Pick(
			        scene, ray, modelDistance, &renderer, excluded)) {
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
		if (std::find(excluded.begin(), excluded.end(), id) != excluded.end()) {
			continue;
		}

		std::array<DirectX::XMFLOAT3, 8> corners;
		const bool missingModel = renderer.IsModelUnavailable(id);
		const bool boundsReady =
		    (!object.HasMesh() || !object.meshRenderer || missingModel)
		        ? showObjectMarkers_ &&
		              ObjectMarkers::GetBounds(object, corners, missingModel)
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

bool EditorScene::FrameDraft(const DirectX::XMFLOAT4X4 &projection) {
	if (playMode_.IsPlaying()) {
		return false;
	}

	try {
		std::vector<AxisAlignedBox> bounds;

		if (stage_ == DialogStage::BOX_CREATION) {
			AxisAlignedBox box;
			if (!boxCreationDialog_.GetPreviewBounds(box)) {
				return false;
			}
			bounds.push_back(box);
		} else if (stage_ == DialogStage::OBJECT_ARRAY &&
		           objectArrayDialog_.CanPreview()) {
			if (!ColliderArrayPreview{}.BuildBounds(
			        document_.GetScene(), objectArrayDialog_.GetSourceIds(),
			        objectArrayDialog_.GetSettings(), bounds)) {
				return false;
			}
		}

		if (bounds.empty()) {
			return false;
		}

		auto combined = bounds.front();
		for (const auto &box : bounds) {
			combined.minimum = {(std::min)(combined.minimum.x, box.minimum.x),
			                    (std::min)(combined.minimum.y, box.minimum.y),
			                    (std::min)(combined.minimum.z, box.minimum.z)};
			combined.maximum = {(std::max)(combined.maximum.x, box.maximum.x),
			                    (std::max)(combined.maximum.y, box.maximum.y),
			                    (std::max)(combined.maximum.z, box.maximum.z)};
		}

		std::array<DirectX::XMFLOAT3, 8> corners;
		for (std::size_t i = 0; i < corners.size(); ++i) {
			corners[i] = {(i & 1) ? combined.maximum.x : combined.minimum.x,
			              (i & 2) ? combined.maximum.y : combined.minimum.y,
			              (i & 4) ? combined.maximum.z : combined.minimum.z};
		}

		if (cameraController_.FrameBounds(corners, projection)) {
			return true;
		}
		Logger::Warning(
		    "EditorScene: 下書き全体を現在の描画範囲に収められませんでした。");
	} catch (const std::exception &error) {
		Logger::Warning(
		    std::format("EditorScene::FrameDraft: {}", error.what()));
	}

	return false;
}

void EditorScene::ShowSceneView() {
	playMode_.RestoreLayout();
	windowFocusRequest_ = "Scene";
	sceneViewPanel_.Show();
}

bool EditorScene::FrameObjects(FrameRequest request,
                               const DirectX::XMFLOAT4X4 &projection,
                               bool runtimeScene) {
	const auto &scene =
	    runtimeScene ? playMode_.GetSession().GetScene() : document_.GetScene();

	if (request == FrameRequest::DRAFT) {
		return !runtimeScene && FrameDraft(projection);
	}

	if (request == FrameRequest::MOTION_TRAIL) {
		std::array<DirectX::XMFLOAT3, 8> corners;

		if (!runtimeScene || !playMode_.IsPlaying() || !showMotionTrail_ ||
		    !motionTrail_.GetFrameBounds(corners)) {
			return false;
		}

		if (!cameraController_.FrameBounds(corners, projection)) {
			Logger::Warning("EditorScene: "
			                "移動軌跡を現在の描画範囲に収められませんでした。");
			return false;
		}

		return true;
	}

	if (request == FrameRequest::RESPAWN || request == FrameRequest::COLLIDER ||
	    request == FrameRequest::JUMP || request == FrameRequest::FOLLOW_PATH) {
		std::array<DirectX::XMFLOAT3, 8> corners;
		bool ready = false;

		if (request == FrameRequest::FOLLOW_PATH) {
			ready = followCameraPreview_.GetFrameBounds(scene, frameObject_,
			                                            corners);
		} else if (request == FrameRequest::JUMP) {
			ready = jumpPreview_.GetFrameBounds(scene, frameObject_, corners);
		} else if (const auto *object = scene.FindObject(frameObject_)) {
			ready = request == FrameRequest::RESPAWN
			            ? RespawnPreview::GetFrameBounds(*object, corners)
			            : ColliderBounds::GetFrameBounds(*object, corners);
		}

		if (!ready) {
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
	const bool includeHidden =
	    request == FrameRequest::ISSUES || request == FrameRequest::INSPECTED;
	const bool selectedOnly =
	    request == FrameRequest::SELECTED || includeHidden;

	for (const auto &[id, object] : scene.GetObjects()) {
		if ((object.IsHidden() && object.HasMesh() && !includeHidden) ||
		    (selectedOnly && !selection.Contains(id))) {
			continue;
		}

		std::array<DirectX::XMFLOAT3, 8> corners;

		const bool missingModel = renderer.IsModelUnavailable(id);
		bool boundsReady =
		    (!object.HasMesh() || !object.meshRenderer || missingModel)
		        ? ObjectFrameBounds::Get(object, corners, missingModel)
		        : renderer.GetObjectBounds(id, corners);
		// 非表示・読み込み不能なモデルも、検証結果や再生状態からはColliderか原点へ寄せられる。
		if (!boundsReady && includeHidden) {
			boundsReady = ObjectFrameBounds::Get(object, corners, true);
		}

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
	UpdatePlaySelectionFollow();
	cameraController_.Update(input_, viewport, deltaTime,
	                         active ? &active->transform.translation : nullptr);
	auto *target = sceneViewPanel_.GetRenderTexture();

	if (!target) {
		return true;
	}

	if (!playSceneRenderer_.SetCameraSettings(sceneProjection_) ||
	    !playSceneRenderer_.SetLighting(playMode_.GetLighting()) ||
	    !playSceneRenderer_.SetCameraPose(cameraController_.GetPose()) ||
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
		if (!playSceneRenderer_.SetCameraSettings(sceneProjection_) ||
		    !playSceneRenderer_.SetLighting(playMode_.GetLighting()) ||
		    !playSceneRenderer_.SetCameraPose(cameraController_.GetPose()) ||
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

	if (showMeasurements_ &&
	    !SelectionMeasurement::AppendLine(playSceneRenderer_, runtime,
	                                      playSelection_.GetIds())) {
		return false;
	}
	if (showMotionTrail_ && !motionTrail_.Update(playSceneRenderer_)) {
		return false;
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

void EditorScene::StopAndSelectEditingObjects() {
	if (!playMode_.IsPlaying() || !playSelectionActive_ || IsBusy() ||
	    playSelection_.GetCount() == 0) {
		return;
	}

	try {
		const auto &runtime = playMode_.GetSession().GetScene();
		const auto &scene = document_.GetScene();
		const auto selected = playSelection_.GetIds();
		std::vector<SceneObjectId> ids(selected.begin(), selected.end());

		for (const auto id : ids) {
			if (!runtime.Contains(id) || !scene.Contains(id)) {
				ShowError(
				    "Every selected object must exist in both the runtime "
				    "and editing scenes. Play and the editing selection "
				    "were kept.");
				return;
			}
		}

		if (!selection_.Set(std::move(ids))) {
			ShowError("Could not select the editing objects. Play was kept.");
			return;
		}

		hierarchyPanel_.RequestReveal(selection_.GetActiveId());
		playMode_.Stop();
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "EditorScene::StopAndSelectEditingObjects: {}", error.what()));
		ShowError("Could not select the editing objects. Play and the editing "
		          "selection were kept. See the log for details.");
	}
}

void EditorScene::UpdatePlaySelectionFollow() {
	const auto id = playSelection_.GetActiveId();
	const auto *object = playMode_.GetSession().GetScene().FindObject(id);

	if (!followPlaySelection_ || !playMode_.IsPlaying() || !object ||
	    !MathUtils::IsFinite(object->transform.translation)) {
		followPlaySelection_ = false;
		followedPlayObject_ = 0;
		return;
	}

	const auto position = object->transform.translation;

	if (followedPlayObject_ != id) {
		followedPlayObject_ = id;
		followedPlayPosition_ = position;
		return;
	}

	const double x = double(position.x) - followedPlayPosition_.x;
	const double y = double(position.y) - followedPlayPosition_.y;
	const double z = double(position.z) - followedPlayPosition_.z;
	const double limit = (std::numeric_limits<float>::max)();
	const bool representable =
	    std::abs(x) <= limit && std::abs(y) <= limit && std::abs(z) <= limit;
	DirectX::XMFLOAT3 offset{};

	if (representable) {
		offset = {static_cast<float>(x), static_cast<float>(y),
		          static_cast<float>(z)};
	}

	if (!representable || !cameraController_.Translate(offset)) {
		followPlaySelection_ = false;
		followedPlayObject_ = 0;
		Logger::Warning(
		    "EditorScene: "
		    "Sceneカメラの追従先を表現できないため追従を解除しました。");
		return;
	}

	followedPlayPosition_ = position;
}

void EditorScene::ClearPlaySelection() {
	motionTrail_.Clear();
	sceneViewPanel_.CancelSelection();
	playSelection_.Clear();
	playHierarchyPanel_.Clear();
	playSelectionActive_ = false;
	followPlaySelection_ = false;
	followedPlayObject_ = 0;
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

void EditorScene::AlignSelectedColliderBounds(AlignmentAxis axis,
                                              bool maximum) {
	if (!HasSelection() || selection_.GetCount() < 2) {
		return;
	}

	try {
		FinishEdit();

		if (!document_.GetHistory().AlignColliderBounds(
		        selection_.GetIds(), selection_.GetActiveId(), axis, maximum)) {
			ShowError("Could not align Collider bounds. Select objects with "
			          "valid Box Colliders and representable coordinates. No "
			          "selected object was changed.");
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "EditorScene::AlignSelectedColliderBounds: {}", error.what()));
		ShowError("Could not align Collider bounds. See the log for details.");
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

void EditorScene::DistributeSelectedColliderGaps(AlignmentAxis axis) {
	if (!HasSelection() || selection_.GetCount() < 3) {
		return;
	}

	try {
		FinishEdit();

		if (!document_.GetHistory().DistributeColliderGaps(selection_.GetIds(),
		                                                   axis)) {
			ShowError(
			    "Could not distribute Collider gaps. Select valid Box "
			    "Colliders with enough space between the fixed endpoints and "
			    "representable coordinates. No selected object was changed.");
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "EditorScene::DistributeSelectedColliderGaps: {}", error.what()));
		ShowError(
		    "Could not distribute Collider gaps. See the log for details.");
	}
}

std::vector<AssetsPanel::TextureSlotChoice>
EditorScene::GetTextureSlotChoices() const {
	std::map<int, AssetsPanel::TextureSlotChoice> choices;

	for (const auto id : selection_.GetIds()) {
		const auto *object = document_.GetScene().FindObject(id);

		if (!object || !object->meshRenderer || !object->HasMesh()) {
			continue;
		}

		const auto model = models_.FindLoaded(object->mesh->modelPath);
		const auto count = models_.GetMaterialCount(model);

		for (std::size_t slot = 0;
		     slot < count && slot <= static_cast<std::size_t>(
		                                 (std::numeric_limits<int>::max)());
		     ++slot) {
			const auto index = static_cast<int>(slot);
			const auto name = models_.GetMaterialName(model, slot);
			const std::string label =
			    name.empty() ? "Unnamed" : std::string{name};
			auto [entry, inserted] = choices.try_emplace(index);
			auto &choice = entry->second;

			if (inserted) {
				choice.index = index;
				choice.name = label;
				choice.selectedCount = selection_.GetCount();
			} else if (choice.name != label) {
				choice.name = "Mixed Materials";
			}

			if (models_.GetMaterialMeshCount(model, slot) != 0) {
				++choice.usedBy;
			}
		}
	}

	std::vector<AssetsPanel::TextureSlotChoice> result;
	result.reserve(choices.size());

	for (auto &[index, choice] : choices) {
		result.push_back(std::move(choice));
	}

	return result;
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

void EditorScene::PlaceSelectionOnWorkPlane() {
	if (!HasSelection()) {
		return;
	}

	try {
		FinishEdit();
		DirectX::XMFLOAT3 translation;
		std::string error;

		if (!ColliderPlacement::GetPlaneTranslation(
		        document_.GetScene(), selection_.GetIds(),
		        sceneGrid_.GetSettings().height, translation, error)) {
			ShowError(std::move(error));
			return;
		}

		if (!document_.GetHistory().TranslateObjects(selection_.GetIds(),
		                                             translation)) {
			ShowError("Could not move the selection to the work plane. No "
			          "selected object was changed.");
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene::PlaceSelectionOnWorkPlane: {}",
		                          error.what()));
		ShowError("Could not place the Colliders on the work plane. See the "
		          "log for details.");
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
		ShowSceneView();
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

void EditorScene::PasteRespawnSettingsToSelection() {
	const auto *settings = clipboard_.GetRespawnSettings();

	if (!HasSelection() || !settings) {
		return;
	}

	try {
		FinishEdit();

		if (!document_.GetHistory().SetRespawnSettings(selection_.GetIds(),
		                                               *settings)) {
			ShowError(
			    "Could not paste all Respawn settings. Check the selected "
			    "objects and Respawn settings. No objects were changed.");
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "EditorScene::PasteRespawnSettingsToSelection: {}", error.what()));
		ShowError(
		    "Could not paste the Respawn settings. See the log for details.");
	}
}

void EditorScene::AssignTextureToSelection(const std::filesystem::path &path,
                                           std::uint32_t slot) {
	if (!HasSelection()) {
		return;
	}

	try {
		FinishEdit();

		if (!document_.GetHistory().SetTexturePaths(selection_.GetIds(), slot,
		                                            path)) {
			ShowError("Could not assign the texture to all selected objects. "
			          "Every target needs valid Mesh Renderer settings. No "
			          "assignments were changed.");
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene::AssignTextureToSelection: {}",
		                          error.what()));
		ShowError("Could not assign the texture. See the log for details.");
	}
}
