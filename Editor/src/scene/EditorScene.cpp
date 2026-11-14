#include "scene/EditorScene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneLighting.h"
#include "ui/UIObjects.h"

#include "assets/AssetFolders.h"
#include "assets/AssetUsage.h"
#include "platform/windows/ShellUtils.h"
#include "assets/ModelPlacement.h"
#include "assets/ModelUsage.h"
#include "assets/PrefabAsset.h"
#include "assets/PrefabInstances.h"
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
#include "gameplay/SceneScripts.h"
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
#include "physics/ColliderArrayPreview.h"
#include "physics/ColliderPlacement.h"
#include "physics/ColliderUtils.h"
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
#include <span>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace {
/// <summary>
/// 指定先が存在しない場合に、選択画面で開ける最も近い親を探す
/// </summary>
std::filesystem::path FindExistingDirectory(std::filesystem::path directory) {
	std::error_code error;
	while (!directory.empty() && !std::filesystem::is_directory(directory, error)) {
		auto parent = directory.parent_path();
		if (parent == directory) {
			return {};
		}
		directory = std::move(parent);
	}
	return directory;
}

/// <summary>
/// 履歴内で同じ名前が並ぶ場合は、場所まで表示して選び分けられるようにする
/// </summary>
std::string RecentPathName(const std::filesystem::path &path,
                           std::span<const std::filesystem::path> recent) {
	const auto name = path.filename();
	const bool repeated = std::ranges::any_of(recent, [&](const auto &other) {
		return other != path && other.filename() == name;
	});
	return PathUtils::ToUtf8(name.empty() || repeated ? path : name);
}

} // namespace

EditorScene::EditorScene(const EngineContext &context)
    : ui_(context.ui), input_(context.input), models_(context.models),
      materials_(context.render.GetMaterials()),
      renderer_(document_.GetScene(), context.models, context.render,
                SceneRenderer::ModelLoadFailure::SKIP),
      playMode_(context.models, context.render, context.audioClips,
                context.audio),
      playSceneRenderer_(playMode_.GetSession().GetScene(), context.models,
                         context.render),
      sceneViewPanel_(context.render),
      audioPreviewPanel_(context.audioClips, context.audio) {
}

EditorScene::~EditorScene() = default;

bool EditorScene::Initialize() {
	renderer_.SetScreenUIEnabled(false);
	playSceneRenderer_.SetScreenUIEnabled(false);

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
	playMode_.SetStartPaused(preferences_.startPlayPaused);
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
	errorScriptPath_.clear();
	errorActionMessage_.clear();
	errorObjectId_ = 0;

	project_ = ProjectDocument{};
	document_.SetProjectRoot({});
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
	audioPreviewPanel_.Clear();

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
	recoverySourceFile_.reset();
	std::string recoveryError;

	const bool recoveryReady =
	    recovery_.Initialize(L"config/recovery", recoveryError);

	if (!recoveryReady) {
		Logger::Error("EditorScene::Recovery: " + recoveryError);
	}

	if (!preferences_.lastProject.empty()) {
		SwitchProject(preferences_.lastProject, false, true);
	}

	startupProjectPicker_ = stage_ == DialogStage::OPEN_PROJECT;
	if (recoveryReady && stage_ == DialogStage::IDLE &&
	    project_.GetRoot().empty()) {
		ShowRecovery(true);
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
	SynchronizePlayScene();

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

	const auto &playSession = playMode_.GetSession();
	const auto *loadedPlayScene = playSession.GetLoadedScenePath();
	const auto playSource = loadedPlayScene ? *loadedPlayScene
	                        : playMode_.IsPlaying() && !playUsesEditingScene_
	                            ? playInitialScenePath_
	                            : std::filesystem::path{};
	const auto &editingScenePath = document_.GetFilePath();
	const bool playSourceIsEditingScene =
	    !playSource.empty() && !editingScenePath.empty() &&
	    PathUtils::IsSameFile(editingScenePath, playSource);
	const auto activePlayObject = playSelection_.GetActiveId();
	const bool canRevealPlayObject =
	    playSelectionActive_ && playSession.GetScene().Contains(activePlayObject) &&
	    playSession.HasSourceObject(activePlayObject) &&
	    (!playSourceIsEditingScene || document_.GetScene().Contains(activePlayObject));
	const auto playSourceObject = canRevealPlayObject ? activePlayObject : SceneObjectId{0};
	const auto sourceToOpen = playSourceIsEditingScene && !canRevealPlayObject
	                              ? std::filesystem::path{} : playSource;
	auto playAction = playMode_.DrawUI(ui_, !IsBusy(), exportConfig_,
	                                   !IsBusy() && playUsesEditingScene_ &&
	                                       playSelectionActive_ &&
	                                       playSelection_.GetCount() != 0,
	                                   &document_.GetScene(),
	                                   document_.GetFilePath(),
	                                   project_.GetStartupScene(), sourceToOpen,
	                                   canRevealPlayObject, lastPlayScenePath_,
	                                   lastPlayCameraOverride_);
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

	if (playAction == EditorPlayMode::Action::STOP_AND_OPEN_SCENE && !IsBusy()) {
		if (!sourceToOpen.empty()) {
			if (!playSourceIsEditingScene) {
				pendingOpenPath_ = playSource;
				StartAction(EditorAction::OPEN_PLAY_SOURCE, playSourceObject);
			} else {
				playMode_.Stop();
				if (playSourceObject != 0) {
					RevealReferenceObject(playSourceObject);
				}
			}
		}
	}

	if (playAction == EditorPlayMode::Action::RENDER_ERROR) {
		playMode_.Stop();
		ShowError("Could not create or resize the Game render target.");
	}

	if ((playAction == EditorPlayMode::Action::PLAY ||
	     playAction == EditorPlayMode::Action::PLAY_STARTUP ||
	     playAction == EditorPlayMode::Action::PLAY_LAST_SCENE) &&
	    !IsBusy()) {
		const auto path = playAction == EditorPlayMode::Action::PLAY_STARTUP
		                      ? project_.GetStartupScene()
		                  : playAction == EditorPlayMode::Action::PLAY_LAST_SCENE
		                      ? lastPlayScenePath_
		                      : std::filesystem::path{};
		const bool replayLastScene = playAction == EditorPlayMode::Action::PLAY_LAST_SCENE;
		const auto camera = replayLastScene ? lastPlayCameraOverride_
		                                    : useRequestedCamera ? requestedCamera : 0;
		StartPlay(path, camera, replayLastScene);
	}

	const bool restartCurrentScene =
	    playAction == EditorPlayMode::Action::RESTART_CURRENT_SCENE;
	if ((playAction == EditorPlayMode::Action::RESTART || restartCurrentScene) &&
	    playMode_.IsPlaying()) {
		RestartPlay(restartCurrentScene);
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
			if (playUsesEditingScene_) {
				const auto ids = selection_.GetIds();
				playSelection_.Set(
				    std::vector<SceneObjectId>(ids.begin(), ids.end()));
			}
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

	SceneObjectId playErrorObject = 0;
	if (playMode_.IsPlaying() && playMode_.IsMaximized()) {
		playFrame_ = true;
		sceneViewPanel_.CancelSelection();

		const auto previousRevision = playSession.GetSceneRevision();
		if (!playMode_.Update(deltaTime, input_, false, false,
		                      &playErrorObject, IsBusy())) {
			ReportPlayUpdateFailure(playErrorObject, previousRevision);
		}

		SynchronizePlayScene();

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

		const auto script = consolePanel_.Draw(ui_, !IsBusy(), true);
		if (!script.path.empty()) {
			std::string error;
			if (scriptEditorDialog_.ShowEdit(script.path, error, script.diagnostic)) {
				if (playFailureReplay_ &&
				    script.logId == playFailureReplay_->logId) {
					scriptReplayPath_ = playFailureReplay_->path;
					scriptReplayCameraOverride_ = playFailureReplay_->camera;
				} else {
					scriptReplayPath_ = lastPlayScenePath_;
					scriptReplayCameraOverride_ = lastPlayCameraOverride_;
				}
				playMode_.Stop();
				ClearPlaySelection();
				stage_ = DialogStage::EDIT_PLAY_SCRIPT;
			} else {
				ShowError(std::move(error));
			}
			UpdateDialogs();
			return true;
		}
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

		const auto previousRevision = playSession.GetSceneRevision();
		if (!sceneViewPanel_.DrawUI(ui_, gizmo_,
		                            TransformGizmo::SelectionMode::SINGLE,
		                            false, true)) {
			playMode_.Stop();
			ShowError("Could not create or resize the Scene render target.");
		} else if (!playMode_.Update(deltaTime, input_,
		                             sceneViewPanel_.GetInput().windowFocused ||
		                                 hierarchy.windowFocused,
		                             sceneViewPanel_.GetRenderTexture() != nullptr,
		                             &playErrorObject, IsBusy())) {
			ReportPlayUpdateFailure(playErrorObject, previousRevision);
		}

		SynchronizePlayScene();

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
		        playSelection_.GetActiveId(),
		        playMode_.GetSession().GetScriptState(
		            playSelection_.GetActiveId()))) {
			SceneWorldTransform world;
			if (!SceneHierarchy::GetWorldTransform(
			        playMode_.GetSession().GetScene(),
			        playSelection_.GetActiveId(), world) ||
			    !clipboard_.CopyTransform(*copied, world.GetPosition())) {
				ShowError("Could not copy an invalid runtime Transform.");
			}
		}

		ui_.BeginDisabled(IsBusy());
		const auto stateRequest =
		    playStatePanel_.Draw(ui_, playMode_.GetSession(), playInitialScenePath_);
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
	const auto *replay = !IsBusy() && action == EditorAction::NONE && playFailureReplay_
	                         ? &*playFailureReplay_ : nullptr;
	const auto consoleScript = consolePanel_.Draw(
	    ui_, !IsBusy(), false, replay ? replay->logId : 0,
	    replay ? replay->path : std::filesystem::path{},
	    replay ? replay->scriptPath : std::filesystem::path{},
	    replay ? std::string_view{replay->inputAction} : std::string_view{});
	if (consoleScript.retryPlay && playFailureReplay_ &&
	    consoleScript.logId == playFailureReplay_->logId) {
		const auto retry = *playFailureReplay_;
		StartPlay(retry.path, retry.camera, true);
		UpdateDialogs();
		return true;
	}
	if (consoleScript.editInput && replay && consoleScript.logId == replay->logId &&
	    !replay->inputAction.empty() && !playMode_.IsPlaying()) {
		FinishEdit();
		gameSettingsDialog_.Show(exportConfig_, project_.GetRoot(), {}, replay->inputAction);
		stage_ = DialogStage::EDIT_CONSOLE_PLAY_INPUT;
		UpdateDialogs();
		return true;
	}
	playStatePanel_.Draw(ui_, playMode_.GetSession(), playInitialScenePath_);

	const bool wasBusy = IsBusy();

	Scene &scene = document_.GetScene();
	EditorHistory &history = document_.GetHistory();

	ui_.BeginDisabled(wasBusy);

	auto checkedRequest = sceneCheckPanel_.Draw(
	    ui_, scene, cameraController_.GetPose(), exportConfig_.actions,
	    history.GetCurrentStateId());
	const bool hasCheckRequest =
	    !checkedRequest.objectIds.empty() || !checkedRequest.scenePath.empty();
	SceneObjectId sceneOpenObject = 0;

	if (!wasBusy && !checkedRequest.assetPath.empty()) {
		assetsPanel_.RequestReveal(checkedRequest.assetPath);
	}

	if (!wasBusy && !checkedRequest.objectIds.empty()) {
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
	inspectorPanel_.Draw(
	    ui_, scene, selection_.GetActiveId(), history,
	    clipboard_.GetTransform() != nullptr, &models_, &assets_,
	    clipboard_.GetCollider() != nullptr,
	    clipboard_.GetCameraLens() != nullptr,
	    clipboard_.GetCharacterSettings() != nullptr,
	    clipboard_.GetFollowSettings() != nullptr,
	    clipboard_.GetRespawnSettings() != nullptr, selection_.GetIds(),
	    clipboard_.GetTextureUVs() != nullptr, exportConfig_.actions);
	const auto prefabRequest = inspectorPanel_.GetPrefabRequest();
	const bool hasPrefabRequest =
	    prefabRequest.action != PrefabInspector::Action::NONE;
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
	const auto &sceneLocation = inspectorPanel_.GetSceneLocationRequest();
	if (!wasBusy && !sceneLocation.empty()) {
		std::string error;
		if (!ShellUtils::RevealLocationInExplorer(sceneLocation, error)) {
			ShowError(std::move(error));
		}
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
	    textureSlots, selection_.GetCount() == 1);
	if (!wasBusy && assetsPanel_.DidRefresh()) {
		sceneCheckPanel_.RequestRecheckIfOpen();
	}
	if (!wasBusy && !assetsPanel_.GetTexturePreviewRequest().empty()) {
		texturePreviewPanel_.Open(assetsPanel_.GetTexturePreviewRequest());
	}

	texturePreviewPanel_.Draw(ui_);

	if (!wasBusy && !assetsPanel_.GetAudioPreviewRequest().empty()) {
		audioPreviewPanel_.Open(assetsPanel_.GetAudioPreviewRequest());
	}

	audioPreviewPanel_.Draw(ui_);
	const auto requestedTexture =
	    assetsPanel_.IsTextureClearRequested()
	        ? std::filesystem::path{}
	        : assetsPanel_.GetTextureSelectionRequest();
	const auto requestedObjects = !assetsPanel_.GetPrefabAppendRequest().empty()
	                                  ? assetsPanel_.GetPrefabAppendRequest()
	                                  : assetsPanel_.GetSceneAppendRequest();
	const auto requestedInstances = assetsPanel_.GetInstanceSelectionRequest();
	const auto &requestedModelReload = checkedRequest.modelReloadPath.empty()
	                                      ? assetsPanel_.GetModelReloadRequest()
	                                      : checkedRequest.modelReloadPath;
	const auto requestedMeshReplacement =
	    assetsPanel_.GetMeshReplacementRequest();
	const auto &requestedTextureReload = checkedRequest.textureReloadPath.empty()
	                                        ? assetsPanel_.GetTextureReloadRequest()
	                                        : checkedRequest.textureReloadPath;
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
	const auto &sceneDrop = sceneViewPanel_.GetObjectDrop();
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

	if (!wasBusy && hasPrefabRequest && !IsBusy() &&
	    action == EditorAction::NONE &&
	    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
		FinishEdit();
		std::string error;
		bool success = true;

		switch (prefabRequest.action) {
		case PrefabInspector::Action::REFRESH:
			success =
			    PrefabInstances::Refresh(history, prefabRequest.root, error);
			break;
		case PrefabInspector::Action::REFRESH_ALL:
			success = PrefabInstances::Refresh(history, prefabRequest.root,
			                                   error, false, true);
			break;
		case PrefabInspector::Action::REVERT:
			success = PrefabInstances::Refresh(history, prefabRequest.root,
			                                   error, true);
			break;
		case PrefabInspector::Action::SAVE_ASSET:
			success = PrefabInstances::SaveToAsset(history, prefabRequest.root,
			                                       error);
			if (success) {
				assets_.Refresh();
			}

			break;
		case PrefabInspector::Action::UNPACK:
			success =
			    PrefabInstances::Unpack(history, prefabRequest.root, error);
			break;
		case PrefabInspector::Action::SELECT_ROOT:
			selection_.SelectOnly(prefabRequest.root);
			hierarchyPanel_.RequestReveal(prefabRequest.root);
			break;
		case PrefabInspector::Action::REVEAL_ASSET:
			assetsPanel_.RequestReveal(
			    scene.FindObject(prefabRequest.root)->prefabLink->asset);
			break;
		case PrefabInspector::Action::NONE:
			break;
		}

		if (!success) {
			ShowError(error);
		}
	}

	if (!wasBusy) {
		const auto &paste = assetsPanel_.GetPasteRequest();
		const bool cutPaste = !paste.sources.empty() && assetsPanel_.IsCutPaste();
		const auto &move = cutPaste ? paste : assetsPanel_.GetMoveRequest();

		if (!move.sources.empty() && !IsBusy() && action == EditorAction::NONE &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			FinishEdit();
			std::string error;

			AssetUsage::Moves moves;
			for (const auto &source : move.sources) {
				if (!PathUtils::IsSameFile(source.parent_path(), move.directory)) {
					moves.emplace_back(source, move.directory / source.filename());
				}
			}

			if (!moves.empty() && !RelocateAssets(moves, error)) {
				ShowError(std::move(error));
				failedAssetMoves_ = std::move(moves);
				failedAssetMoveWasCutPaste_ = cutPaste;
			} else if (cutPaste) {
				assetsPanel_.FinishPaste();
			}
		}

		const auto &rename = assetsPanel_.GetRenameRequest();

		if (!rename.empty() && !IsBusy() && action == EditorAction::NONE &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			FinishEdit();
			pendingRenamePath_ = rename;
			assetRenameName_ = PathUtils::ToUtf8(rename.filename());
			assetRenameError_.clear();
			stage_ = DialogStage::RENAME_ASSET;
		}

		const bool copyPaste = !paste.sources.empty() && !cutPaste;
		const auto &duplicate =
		    copyPaste ? paste.sources : assetsPanel_.GetDuplicateRequest();

		if (!duplicate.empty() && !IsBusy() && action == EditorAction::NONE &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			std::string error;
			const auto directory = copyPaste ? paste.directory : std::filesystem::path{};
			if (!CopyAssets(duplicate, directory, error)) {
				ShowError(std::move(error));
				failedAssetCopy_ = {duplicate, directory};
			}
		}

		const auto &recycle = assetsPanel_.GetRecycleRequest();
		const auto &references = assetsPanel_.GetReferenceRequest();

		if ((!recycle.empty() || !references.empty()) && !IsBusy() &&
		    action == EditorAction::NONE &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			FinishEdit();
			const bool moveToRecycleBin = !recycle.empty();
			if (moveToRecycleBin) {
				suspendedRecyclePaths_.clear();
			}
			pendingAssetPaths_ = moveToRecycleBin ? recycle : references;
			assetReferenceFilter_.clear();
			CheckAssetReferences(moveToRecycleBin);
			stage_ = moveToRecycleBin ? DialogStage::RECYCLE_ASSET
			                          : DialogStage::ASSET_REFERENCES;
		}

		if (!requestedObjects.empty() && !IsBusy() &&
		    action == EditorAction::NONE &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			AppendAssetObjects(requestedObjects);
		}

		const auto &inspectorScene = inspectorPanel_.GetSceneOpenRequest();
		const auto &playScene = assetsPanel_.GetScenePlayRequest();
		if (!playScene.empty() && !IsBusy() &&
		    action == EditorAction::NONE &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			pendingOpenPath_ = playScene;
			action = EditorAction::PLAY_ASSET_SCENE;
		}
		const auto &requestedScene = !checkedRequest.scenePath.empty()
		                                 ? checkedRequest.scenePath
		                                 : inspectorScene.empty()
		                                       ? assetsPanel_.GetSceneOpenRequest()
		                                       : inspectorScene;
		if (!requestedScene.empty() && !IsBusy() &&
		    action == EditorAction::NONE &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			sceneOpenObject = !checkedRequest.scenePath.empty()
			                      ? checkedRequest.destinationObjectId
			                      : inspectorPanel_.GetSceneOpenObject();
			const bool sameScene =
			    !document_.GetFilePath().empty() &&
			    PathUtils::IsSameFile(requestedScene, document_.GetFilePath());
			if (!sameScene) {
				// 保存確認が終わるまで、表示中の選択が変わっても開く対象を保持する。
				pendingOpenPath_ = requestedScene;
				action = EditorAction::OPEN_PATH;
			} else if (sceneOpenObject != 0) {
				FinishEdit();
				RevealReferenceObject(sceneOpenObject);
			}
		}

		const auto &requestedSettings = assetsPanel_.GetGameSettingsRequest();
		if (!checkedRequest.inputAction.empty() && !IsBusy() &&
		    action == EditorAction::NONE &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			FinishEdit();
			gameSettingsDialog_.Show(exportConfig_, project_.GetRoot(), {},
			                         std::move(checkedRequest.inputAction));
			stage_ = DialogStage::GAME_SETTINGS;
		}
		if (!requestedSettings.empty() && !IsBusy() &&
		    action == EditorAction::NONE &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			pendingOpenPath_ = requestedSettings;
			action = EditorAction::LOAD_GAME_SETTINGS;
		}

		const auto &inspectorScript = inspectorPanel_.GetScriptEditRequest();
		const auto &scriptEdit = !checkedRequest.scriptPath.empty() ? checkedRequest.scriptPath
		                         : !consoleScript.path.empty() ? consoleScript.path
		                         : !inspectorScript.empty() ? inspectorScript
		                                                   : assetsPanel_.GetScriptEditRequest();
		if ((assetsPanel_.IsScriptCreateRequested() || !scriptEdit.empty()) &&
		    !IsBusy() && action == EditorAction::NONE &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			FinishEdit();
			std::string error;
			const bool opened =
			    !scriptEdit.empty()
			        ? scriptEditorDialog_.ShowEdit(
			              scriptEdit, error, checkedRequest.scriptPath.empty()
			                                     ? consoleScript.diagnostic
			                                     : checkedRequest.scriptDiagnostic,
			              checkedRequest.scriptPath.empty() ? consoleScript.diagnosticPath
			                                                : std::filesystem::path{})
			        : scriptEditorDialog_.ShowCreate(
			              assets_.GetRootDirectory(),
			              assetsPanel_.GetOpenDirectory(), error);
			if (opened) {
				stage_ = checkedRequest.scriptPath.empty() ? DialogStage::EDIT_SCRIPT
				                                          : DialogStage::EDIT_SCENE_CHECK_SCRIPT;
				if (checkedRequest.scriptPath.empty() && playFailureReplay_ &&
				    consoleScript.logId == playFailureReplay_->logId) {
					scriptReplayPath_ = playFailureReplay_->path;
					scriptReplayCameraOverride_ = playFailureReplay_->camera;
					stage_ = DialogStage::EDIT_PLAY_SCRIPT;
				}
			} else {
				ShowError(error);
			}
		}

		const auto &clipEdit = assetsPanel_.GetAnimationClipEditRequest();
		if ((assetsPanel_.IsAnimationClipCreateRequested() || !clipEdit.empty()) &&
		    !IsBusy() && action == EditorAction::NONE &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			FinishEdit();
			try {
				std::string error;
				if (clipEdit.empty()) {
					animationClipEditor_.ShowCreate(AssetFolders::Resolve(
					    assets_.GetRootDirectory(), assetsPanel_.GetOpenDirectory()));
					stage_ = DialogStage::EDIT_ANIMATION_CLIP;
				} else if (animationClipEditor_.ShowEdit(clipEdit, error)) {
					stage_ = DialogStage::EDIT_ANIMATION_CLIP;
				} else {
					ShowError(std::move(error));
				}
			} catch (const std::exception &error) {
				ShowError(error.what());
			}
		}

		if (assetsPanel_.IsPrefabCreateRequested() &&
		    selection_.GetCount() == 1 && !IsBusy() &&
		    action == EditorAction::NONE &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			FinishEdit();
			std::string error;
			if (prefabCreateDialog_.Show(
			        scene, selection_.GetActiveId(), assets_.GetRootDirectory(),
			        assetsPanel_.GetOpenDirectory(), error)) {
				stage_ = DialogStage::CREATE_PREFAB;
			} else {
				ShowError(error);
			}
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

		if (assetsPanel_.IsAudioImportRequested() && !IsBusy() &&
		    action == EditorAction::NONE &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			try {
				FinishEdit();
				audioImportDialog_.Show(assets_,
				                        assetsPanel_.GetOpenDirectory());
				stage_ = DialogStage::IMPORT_AUDIO;
			} catch (const std::exception &error) {
				Logger::Error(
				    std::format("EditorScene::ImportAudio: {}", error.what()));
				ShowError(
				    "Could not open audio import. See the log for details.");
			}
		}

		if (hierarchyRequest.action != HierarchyPanel::Action::NONE) {
			// 編集中の内容を、物体を消す前に確定する
			FinishEdit();

			try {
				switch (hierarchyRequest.action) {
				case HierarchyPanel::Action::REPARENT_OBJECTS:
					if (!document_.GetHistory().SetParent(
					        hierarchyRequest.parentObjects,
					        hierarchyRequest.parentId,
					        hierarchyRequest.keepWorld)) {
						ShowError("Could not change parent. A parent cannot be "
						          "one of its descendants. If the world "
						          "transform requires shear, hold Alt while "
						          "dropping to keep the local transform.");
					} else {
						hierarchyPanel_.RequestReveal(selection_.GetActiveId());
					}

					break;
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
				case HierarchyPanel::Action::ENABLE_OBJECTS:
				case HierarchyPanel::Action::DISABLE_OBJECTS:
					if (!document_.GetHistory().SetActive(
					        selection_.GetIds(),
					        hierarchyRequest.action ==
					            HierarchyPanel::Action::ENABLE_OBJECTS)) {
						ShowError("Could not change object activity.");
					}

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
			ReloadAsset(requestedModelReload, false);
		}

		if (!IsBusy() && action == EditorAction::NONE &&
		    !requestedTextureReload.empty() &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			ReloadAsset(requestedTextureReload, true);
		}

		if (!IsBusy() && action == EditorAction::NONE && textureAssignment &&
		    !hasCheckRequest && requestedTextureUsers.empty() &&
		    requestedObjects.empty() && requestedInstances.empty() &&
		    requestedModelPath.empty() && !hasAssetDrop &&
		    modelRequest.objectId == 0 && visibilityRequest.objectId == 0 &&
		    colliderFitRequest == 0 && !hasObjectRequest && !historyRequest &&
		    hierarchyRequest.action == HierarchyPanel::Action::NONE) {
			AssignTextureToSelection(requestedTexture,
			                         assetsPanel_.GetTextureSlot());
		}

		if (!IsBusy() && action == EditorAction::NONE &&
		    !requestedTextureUsers.empty() && requestedObjects.empty() &&
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
			    requestedTextureUsers.empty() && requestedObjects.empty() &&
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
			    requestedTextureUsers.empty() && requestedObjects.empty() &&
			    !historyRequest) {
				const bool objectPanelFocused =
				    hierarchyRequest.windowFocused ||
				    sceneViewPanel_.GetInput().windowFocused;

				action = ReadShortcut(objectPanelFocused);
			}

			if (action != EditorAction::NONE) {
				StartAction(action, action == EditorAction::OPEN_PATH
				                        ? sceneOpenObject : 0);
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
	    hasCheckRequest || hasPrefabRequest || textureAssignment ||
	    !requestedMeshReplacement.empty() || !requestedModelReload.empty() ||
	    !requestedTextureReload.empty() || !requestedModelPath.empty() ||
	    !requestedInstances.empty() || !requestedTextureUsers.empty() ||
	    !requestedObjects.empty() || hasAssetDrop ||
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
				SceneWorldTransform world;

				if (SceneHierarchy::GetWorldTransform(
				        scene, selection_.GetActiveId(), world)) {
					orbitTransform.translation = world.GetPosition();
					orbitTarget = &orbitTransform.translation;
				}
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
			                        : PlaceDroppedObjects(drop.path, ray);
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
	Transform singleGizmo;
	const Transform beforeLocal = selected ? selected->transform : Transform{};

	if (multipleSelection) {
		if (gizmoEditActive_ ||
		    MakeSelectionTransform(selectionGizmoTransform_)) {
			transform = &selectionGizmoTransform_;
		}
	} else if (HasSingleSelection() && selected) {
		SceneWorldTransform world;

		if (SceneHierarchy::GetWorldTransform(scene, activeId, world)) {
			singleGizmo = selected->transform;
			singleGizmo.translation = world.GetPosition();
			singleGizmo.rotation = world.rotation;
			transform = &singleGizmo;
		}
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
			} else if (!history.BeginTransform(activeId, beforeLocal)) {
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

		if (!multipleSelection) {
			switch (gizmo_.GetOperation()) {
			case TransformGizmo::Operation::TRANSLATE:
				applied = SceneHierarchy::SetWorldPosition(
				    scene, activeId, singleGizmo.translation);
				break;
			case TransformGizmo::Operation::ROTATE:
				applied = SceneHierarchy::SetWorldPose(scene, activeId,
				                                       singleGizmo.translation,
				                                       singleGizmo.rotation);
				break;
			case TransformGizmo::Operation::SCALE:
				// 単体の拡縮は親のワールド拡縮ではなく、Inspectorと同じローカルの値を編集する。
				if (TransformUtils::IsValid(singleGizmo))
					selected->transform.scale = singleGizmo.scale;
				else
					applied = false;
				break;
			}

			if (!applied)
				FinishEdit();
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
		auto current = preferences_;
		current.showGrid = showGrid_;
		current.showSelectionBounds = showSelectionBounds_;
		current.showCameraGuides = showCameraGuides_;
		current.showRespawnGuides = showRespawnGuides_;
		current.showJumpGuide = showJumpGuide_;
		current.showMotionTrail = showMotionTrail_;
		current.maximizeGameOnPlay = playMode_.GetMaximizeOnPlay();
		current.startPlayPaused = playMode_.GetStartPaused();
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
		if (project_.GetRoot().empty()) {
			current.gameExport = exportConfig_;
			current.assets = assetSources_;
		} else {
			current.lastProject = project_.GetRoot();
			current.RememberProject(project_.GetRoot());
			if (failedRestoredScene_.empty()) {
				current.RememberProjectScene(project_.GetRoot(), document_.GetFilePath());
			}
			current.RememberProjectAssetFolder(project_.GetRoot(),
			                                  assetsPanel_.GetOpenDirectory());
		}
		current.RememberSceneView(document_.GetFilePath(), cameraController_.GetViewState());
		current.RememberSceneSelection(document_.GetFilePath(), selection_.GetIds());
		const auto recent = recentScenes_.GetPaths();
		current.recentScenes.assign(recent.begin(), recent.end());

		if (!(current == preferences_) && current.Save()) {
			preferences_ = current;
		}
	}

	texturePreviewPanel_.Clear();
	audioPreviewPanel_.Clear();
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
	errorScriptPath_.clear();
	errorActionMessage_.clear();
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
	std::string name;
	if (recoverySourceFile_) {
		name = recoverySourceFile_->empty()
		           ? "Recovered Scene"
		           : "Recovered: " + PathUtils::ToUtf8(recoverySourceFile_->filename());
	} else {
		name = document_.GetFilePath().empty()
		           ? "Untitled"
		           : PathUtils::ToUtf8(document_.GetFilePath().filename());
	}

	if (!project_.GetRoot().empty()) {
		return std::format("{}{} - {} - Editor", name,
		                   document_.IsModified() ? "*" : "",
		                   project_.GetGameConfig().title);
	}

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

		if (ui_.MenuItem("New Project...", nullptr, enabled)) {
			action = EditorAction::NEW_PROJECT;
		}

		if (ui_.MenuItem("Open Project...", nullptr, enabled)) {
			action = EditorAction::OPEN_PROJECT;
		}

		const auto &recentProjects = preferences_.recentProjects;
		if (ImGui::BeginMenu("Recent Projects",
		                     enabled && !recentProjects.empty())) {
			const auto &activeRoot = project_.GetRoot();
			std::size_t index = 0;
			for (const auto &root : recentProjects) {
				const auto label = std::format("{}###RecentProject{}",
				                               RecentPathName(root, recentProjects), index++);
				const bool isCurrent =
				    !activeRoot.empty() && PathUtils::IsSameFile(activeRoot, root);
				if (ImGui::MenuItem(label.c_str(), nullptr, isCurrent,
				                    !isCurrent)) {
					pendingOpenPath_ = root;
					action = EditorAction::OPEN_PROJECT_PATH;
				}
				ImGui::SetItemTooltip("%s", PathUtils::ToUtf8(root).c_str());
			}
			ui_.Separator();
			if (ui_.MenuItem("Remove Missing Projects") &&
			    preferences_.RemoveMissingProjects(activeRoot)) {
				SaveEditorSession(true);
			}
			ImGui::SetItemTooltip("Remove entries whose folder or project.json is missing.");
			ui_.EndMenu();
		}

		const auto startup = project_.GetStartupScene();
		const auto &current = document_.GetFilePath();
		const bool currentIsStartup = !startup.empty() && !current.empty() &&
		                              PathUtils::IsSameFile(startup, current);
		const bool canOpenStartup = enabled && !startup.empty() && !currentIsStartup;
		if (ui_.MenuItem("Open Startup Scene", nullptr, canOpenStartup)) {
			pendingOpenPath_ = startup;
			action = EditorAction::OPEN_PATH;
		}
		if (!startup.empty()) {
			ImGui::SetItemTooltip("%s", PathUtils::ToUtf8(startup).c_str());
		}
		if (!failedRestoredScene_.empty()) {
			if (ui_.MenuItem("Retry Last Edited Scene", nullptr, enabled)) {
				pendingOpenPath_ = failedRestoredScene_;
				action = EditorAction::OPEN_PATH;
			}
			ImGui::SetItemTooltip("%s", PathUtils::ToUtf8(failedRestoredScene_).c_str());
			if (ui_.MenuItem("Locate Last Edited Scene...", nullptr, enabled)) {
				action = EditorAction::BROWSE_LAST_EDITED_SCENE;
			}
			if (ui_.MenuItem("Resume Current Scene Next Time", nullptr,
			                 enabled && project_.CanUseAsStartupScene(current))) {
				action = EditorAction::RESUME_CURRENT_SCENE;
			}
		}

		const bool canSetCurrentStartup =
		    enabled && !currentIsStartup && project_.CanUseAsStartupScene(current);
		if (ui_.MenuItem("Set Current Scene as Startup", nullptr,
		                 canSetCurrentStartup)) {
			pendingOpenPath_ = current;
			action = EditorAction::SET_STARTUP_SCENE;
		}
		if (enabled && !canSetCurrentStartup && !currentIsStartup &&
		    ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled |
		                         ImGuiHoveredFlags_ForTooltip)) {
			ImGui::SetTooltip(
			    "%s", project_.GetRoot().empty()
			              ? "Open a project first."
			              : "Save the scene in the project's scenes folder first.");
		}

		const auto &projectScenes = assets_.GetSceneTargets();
		const bool canBrowseProjectScenes =
		    enabled && !project_.GetRoot().empty() && !projectScenes.empty();
		if (ImGui::BeginMenu("Choose Startup Scene",
		                     enabled && !project_.GetRoot().empty())) {
			const auto directory = project_.GetScenesDirectory();
			for (const auto &path : projectScenes) {
				const auto label = PathUtils::ToUtf8(path.lexically_relative(directory));
				const bool selected =
				    !startup.empty() && PathUtils::IsSameFile(path, startup);
				if (ImGui::MenuItem(label.c_str(), nullptr, selected, !selected)) {
					pendingOpenPath_ = path;
					action = EditorAction::SET_STARTUP_SCENE;
				}
			}
			if (!projectScenes.empty())
				ui_.Separator();
			if (ui_.MenuItem("Browse...")) {
				action = EditorAction::BROWSE_STARTUP_SCENE;
			}
			if (ui_.MenuItem("Clear Startup Scene", nullptr, !startup.empty())) {
				action = EditorAction::CLEAR_STARTUP_SCENE;
			}
			ImGui::SetItemTooltip("Play and export will use the scene you are editing.");
			ui_.EndMenu();
		}

		ui_.Separator();

		if (ui_.MenuItem("New", "Ctrl+N", enabled)) {
			action = EditorAction::NEW;
		}

		if (ui_.MenuItem("New Sample Stage...", nullptr, enabled)) {
			action = EditorAction::NEW_SAMPLE;
		}

		if (ui_.MenuItem("Open...", "Ctrl+O", enabled)) {
			action = EditorAction::OPEN;
		}

		if (ImGui::BeginMenu("Project Scenes", canBrowseProjectScenes)) {
			const auto directory = project_.GetScenesDirectory();
			for (const auto &path : projectScenes) {
				const auto relative = path.lexically_relative(directory);
				const bool isCurrent =
				    !current.empty() && PathUtils::IsSameFile(path, current);
				const auto label = PathUtils::ToUtf8(relative);
				if (ImGui::MenuItem(label.c_str(), nullptr, isCurrent,
				                    !isCurrent)) {
					pendingOpenPath_ = path;
					action = EditorAction::OPEN_PATH;
				}
			}
			ui_.EndMenu();
		}

		if (ui_.MenuItem("Import Game Scene...", nullptr, enabled)) {
			action = EditorAction::IMPORT_SCENE;
		}

		const auto recent = recentScenes_.GetPaths();
		const auto *previous = FindPreviousScene();
		if (ui_.MenuItem("Open Previous Scene", "Ctrl+Shift+O",
		                 enabled && previous)) {
			action = EditorAction::OPEN_PREVIOUS_SCENE;
		}
		if (previous) {
			ImGui::SetItemTooltip("%s", PathUtils::ToUtf8(*previous).c_str());
		}

		ui_.BeginDisabled(!enabled || recent.empty());

		if (ui_.BeginMenu("Open Recent")) {
			std::size_t index = 0;
			bool hasMissing = false;

			for (const auto &path : recent) {
				std::error_code status;
				const bool missing = std::filesystem::status(path, status).type() ==
				                     std::filesystem::file_type::not_found;
				hasMissing |= missing;
				const auto label =
				    std::format("{}{}###RecentScene{}",
				                RecentPathName(path, recent),
				                missing ? " (Missing)" : "", index++);

				if (ui_.MenuItem(label.c_str(), nullptr, enabled)) {
					// 保存確認中に一覧が更新されても、開くパスは変えない
					pendingOpenPath_ = path;
					action = EditorAction::OPEN_PATH;
				}

				ImGui::SetItemTooltip("%s", PathUtils::ToUtf8(path).c_str());
			}

			ui_.Separator();

			if (ui_.MenuItem("Remove Missing Scenes", nullptr, enabled && hasMissing)) {
				recentScenes_.RemoveMissing();
				SaveEditorSession();
			}
			if (ui_.MenuItem("Clear Recent", nullptr, enabled)) {
				recentScenes_.Clear();
				SaveEditorSession();
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

		if (ui_.MenuItem("Assets Folder...", nullptr,
		                 enabled && project_.GetRoot().empty())) {
			action = EditorAction::ASSETS_FOLDER;
		}
		if (!suspendedRecyclePaths_.empty() &&
		    ui_.MenuItem("Resume Move to Recycle Bin...", nullptr,
		                 enabled && !playMode_.IsPlaying())) {
			action = EditorAction::RESUME_ASSET_RECYCLE;
		}

		if (ui_.MenuItem("Scene Settings...", nullptr, enabled)) {
			action = EditorAction::SCENE_SETTINGS;
		}

		if (ui_.MenuItem("Game Settings...", nullptr, enabled)) {
			action = EditorAction::GAME_SETTINGS;
		}

		if (ui_.MenuItem("Export Game...", nullptr, enabled)) {
			action = EditorAction::EXPORT_GAME;
		}

		if (ui_.MenuItem("Open Existing Game Export...", nullptr, enabled)) {
			pendingOpenPath_.clear();
			action = EditorAction::OPEN_GAME_EXPORT;
		}

		const auto lastExport = preferences_.LastGameExport(project_.GetRoot());
		if (ui_.MenuItem("Open Last Game Export", nullptr, enabled && !lastExport.empty())) {
			pendingOpenPath_ = lastExport;
			action = EditorAction::OPEN_GAME_EXPORT;
		}
		if (!lastExport.empty()) {
			ImGui::SetItemTooltip("%s", PathUtils::ToUtf8(lastExport).c_str());
		}

		const auto recentExports = preferences_.RecentGameExports(project_.GetRoot());
		if (ImGui::BeginMenu("Recent Game Exports",
		                     enabled && (!recentExports.empty() || !lastExport.empty()))) {
			std::size_t index = 0;
			for (const auto &directory : recentExports) {
				const auto problem = FileDialog::GameExportOpenProblem(directory);
				const auto label = std::format("{}{}###RecentGameExport{}",
				                               RecentPathName(directory, recentExports),
				                               problem.empty() ? "" : " (unavailable)", index++);
				if (ImGui::MenuItem(label.c_str())) {
					pendingOpenPath_ = directory;
					action = EditorAction::OPEN_GAME_EXPORT;
				}
				const auto tooltip = problem.empty()
				    ? PathUtils::ToUtf8(directory)
				    : std::format("{}\n{}", PathUtils::ToUtf8(directory), problem);
				ImGui::SetItemTooltip("%s", tooltip.c_str());
			}
			ui_.Separator();
			if (ui_.MenuItem("Remove Missing Export Folders") &&
			    preferences_.RemoveMissingGameExports(project_.GetRoot())) {
				SaveEditorSession(true);
			}
			ImGui::SetItemTooltip("Remove history entries whose export folders no longer exist.");
			ui_.EndMenu();
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

		if (ui_.BeginMenu("Colliders")) {
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

			if (ui_.MenuItem("Paste Colliders", nullptr,
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

		if (ui_.BeginMenu("UI")) {
			if (ui_.MenuItem("Canvas", nullptr, enabled))
				action = EditorAction::CREATE_CANVAS;
			if (ui_.MenuItem("Text", nullptr, enabled))
				action = EditorAction::CREATE_UI_TEXT;
			if (ui_.MenuItem("Button", nullptr, enabled))
				action = EditorAction::CREATE_UI_BUTTON;
			if (ui_.MenuItem("Image", nullptr, enabled))
				action = EditorAction::CREATE_UI_IMAGE;
			ui_.EndMenu();
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
		if (shift && !alt) {
			return EditorAction::OPEN_PREVIOUS_SCENE;
		}
		return EditorAction::OPEN;
	}

	if (keyboard.IsPressed(Key::N)) {
		return EditorAction::NEW;
	}

	return EditorAction::NONE;
}

const std::filesystem::path *EditorScene::FindPreviousScene() const {
	const auto &current = document_.GetFilePath();
	const bool projectOpen = !project_.GetRoot().empty();
	for (const auto &path : recentScenes_.GetPaths()) {
		std::error_code status;
		if ((current.empty() || !PathUtils::IsSameFile(path, current)) &&
		    (projectOpen ? project_.CanUseAsStartupScene(path)
		                 : std::filesystem::is_regular_file(path, status))) {
			return &path;
		}
	}
	return nullptr;
}

void EditorScene::StartAction(EditorAction action, SceneObjectId openObject) {
	if (stage_ != DialogStage::IDLE || closeApproved_ ||
	    action == EditorAction::NONE ||
	    ((action == EditorAction::IMPORT_SCENE ||
	      action == EditorAction::ASSETS_FOLDER ||
	      action == EditorAction::RESUME_ASSET_RECYCLE ||
	      action == EditorAction::OPEN_PATH ||
	      action == EditorAction::BROWSE_LAST_EDITED_SCENE ||
	      action == EditorAction::OPEN_PREVIOUS_SCENE ||
	      action == EditorAction::PLAY_ASSET_SCENE ||
	      action == EditorAction::OPEN_EXPORT_ISSUE ||
	      action == EditorAction::OPEN_PROJECT_PATH ||
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
	case EditorAction::PLAY_ASSET_SCENE:
		StartPlay(std::exchange(pendingOpenPath_, std::filesystem::path{}), 0);
		return;
	case EditorAction::OPEN_PREVIOUS_SCENE:
		if (const auto *path = FindPreviousScene()) {
			pendingOpenPath_ = *path;
			pendingOpenObject_ = 0;
			pendingAction_ = EditorAction::OPEN_PATH;
			ConfirmSceneChanges();
		}
		return;
	case EditorAction::RESUME_CURRENT_SCENE:
		if (!failedRestoredScene_.empty() &&
		    project_.CanUseAsStartupScene(document_.GetFilePath())) {
			auto previous = std::exchange(failedRestoredScene_, {});
			SaveEditorSession();
			const auto saved = preferences_.LastProjectScene(project_.GetRoot());
			if (saved.empty() || !PathUtils::IsSameFile(saved, document_.GetFilePath())) {
				failedRestoredScene_ = std::move(previous);
				ShowError("Could not save the scene to resume next time.");
			}
		}
		return;
	case EditorAction::SET_STARTUP_SCENE: {
		const auto path = std::exchange(pendingOpenPath_, std::filesystem::path{});
		std::string error;
		if (!SetStartupScene(path, error))
			ShowError(std::move(error));
		return;
	}
	case EditorAction::CLEAR_STARTUP_SCENE: {
		const auto previous = project_.GetStartupScene();
		std::string error;
		if (!project_.Save(project_.GetGameConfig(), {}, error)) {
			ShowError(std::move(error));
		} else if (!previous.empty()) {
			exportDialog_.FollowStartupScene(project_.GetRoot(), previous,
			                                 document_.GetFilePath());
			SaveEditorSession();
		}
		return;
	}
	case EditorAction::BROWSE_STARTUP_SCENE:
		BrowseStartupScene();
		stage_ = DialogStage::SELECT_STARTUP_SCENE;
		return;

	case EditorAction::RECOVER_SCENE:
		ShowRecovery();
		return;

	case EditorAction::SAVE_SELECTION:
		StartSaveSelection();
		return;

	case EditorAction::RESUME_ASSET_RECYCLE:
		if (!suspendedRecyclePaths_.empty()) {
			pendingAssetPaths_ = std::exchange(suspendedRecyclePaths_, {});
			assetReferenceFilter_ = std::exchange(suspendedRecycleFilter_, {});
			CheckAssetReferences(true);
			stage_ = DialogStage::RECYCLE_ASSET;
		}
		return;
	case EditorAction::ASSETS_FOLDER:
		try {
			fileDialog_.Show(FileDialogMode::SELECT_DIRECTORY,
			                 assets_.GetRootDirectory(), FileDialogFilter::SCENE,
			                 "Choose Assets Folder###FileDialog");
			stage_ = DialogStage::SELECT_ASSET_FOLDER;
		} catch (const std::exception &error) {
			Logger::Error(
			    std::format("EditorScene::AssetsFolder: {}", error.what()));
			ShowError(
			    "Could not select an Assets folder. See the log for details.");
		}

		return;

	case EditorAction::SCENE_SETTINGS: {
		FinishEdit();
		const auto &lighting = document_.GetScene().GetLighting();
		sceneAmbientOverride_ = lighting.has_value();
		sceneAmbientDraft_ = lighting ? lighting->ambient : exportConfig_.lighting.ambient;
		sceneFogDraft_ = document_.GetScene().GetFog();
		sceneSettingsOpenRequested_ = true;
		stage_ = DialogStage::SCENE_SETTINGS;
		return;
	}

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
		ExecutePendingAction();
		return;
	case EditorAction::WRITE_GAME_EXPORT:
		pendingAction_ = action;
		ConfirmSceneChanges();
		return;

	case EditorAction::OPEN_GAME_EXPORT: {
		auto directory = std::exchange(pendingOpenPath_, std::filesystem::path{});
		pendingGameExportReplacement_.clear();
		if (OpenGameExport(directory)) {
			return;
		}

		pendingGameExportReplacement_ = directory;
		if (directory.empty()) {
			const auto recent = preferences_.RecentGameExports(project_.GetRoot());
			if (!recent.empty())
				directory = recent.front();
		}
		directory = FindExistingDirectory(std::move(directory));
		if (directory.empty())
			directory = project_.GetRoot();
		fileDialog_.Show(FileDialogMode::SELECT_DIRECTORY, directory,
		                 FileDialogFilter::GAME_EXPORT,
		                 "Open Game Export###FileDialog");
		if (!pendingGameExportReplacement_.empty()) {
			fileDialog_.RetrySelection(
			    std::format("{}\n{}\nChoose a valid export folder to update the history.",
			                PathUtils::ToUtf8(pendingGameExportReplacement_),
			                FileDialog::GameExportOpenProblem(pendingGameExportReplacement_)),
			    pendingGameExportReplacement_);
		}
		stage_ = DialogStage::SELECT_GAME_EXPORT;
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

	case EditorAction::OPEN:
	case EditorAction::BROWSE_LAST_EDITED_SCENE: {
		const bool locating = action == EditorAction::BROWSE_LAST_EDITED_SCENE;
		locatedScenePreviousPath_ = locating ? failedRestoredScene_ : std::filesystem::path{};
		openSceneDialog_.Show(FileDialogMode::OPEN,
		                      GetSceneDialogDirectory(locating ? failedRestoredScene_
		                                                       : document_.GetFilePath()),
		                      FileDialogFilter::SCENE,
		                      locating ? "Locate Last Edited Scene###FileDialog" : "");
		if (!project_.GetRoot().empty()) {
			assets_.Refresh();
			openSceneDialog_.SetProjectScenes(project_.GetRoot(), assets_.GetSceneTargets(),
			                                  locating || preferences_.searchProjectScenes);
		}
		stage_ = DialogStage::OPEN_FILE;
		return;
	}

	case EditorAction::NEW_PROJECT:
	case EditorAction::OPEN_PROJECT: {
		const auto directory = project_.GetRoot().empty()
		                           ? document_.GetFilePath().parent_path()
		                           : project_.GetRoot();
		const bool creating = action == EditorAction::NEW_PROJECT;
		projectDialog_.Show(FileDialogMode::SELECT_DIRECTORY, directory,
		                 creating ? FileDialogFilter::SCENE : FileDialogFilter::PROJECT,
		                 creating ? "New Project###FileDialog" : "Open Project###FileDialog");
		stage_ = creating
		             ? DialogStage::CREATE_PROJECT
		             : DialogStage::OPEN_PROJECT;
		return;
	}

	case EditorAction::OPEN_PROJECT_PATH:
	case EditorAction::NEW:
	case EditorAction::NEW_SAMPLE:
	case EditorAction::RESTORE_RECOVERY:
	case EditorAction::IMPORT_SCENE:
	case EditorAction::EXIT:
		// 保存確認が終わるまで、実際の操作は保留する
		pendingAction_ = action;

		ConfirmSceneChanges();

		return;

	case EditorAction::OPEN_PATH:
	case EditorAction::OPEN_PLAY_SOURCE:
	case EditorAction::OPEN_EXPORT_ISSUE:
		pendingOpenObject_ = openObject;
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
			    PathUtils::ToUtf8(document_.GetFilePath()),
			    document_.IsModified());
			stage_ = DialogStage::CONFIRM_RELOAD;
		}

		return;

	case EditorAction::SAVE_COPY:
		pendingAction_ = EditorAction::NONE;
		StartSaveCopy();
		return;
	case EditorAction::CREATE_CANVAS:
	case EditorAction::CREATE_UI_TEXT:
	case EditorAction::CREATE_UI_IMAGE:
	case EditorAction::CREATE_UI_BUTTON: {
		FinishEdit();
		std::string error;
		const auto kind =
		    action == EditorAction::CREATE_CANVAS      ? UIObjects::Kind::CANVAS
		    : action == EditorAction::CREATE_UI_TEXT   ? UIObjects::Kind::TEXT
		    : action == EditorAction::CREATE_UI_BUTTON ? UIObjects::Kind::BUTTON
		                                               : UIObjects::Kind::IMAGE;
		const auto id =
		    UIObjects::Create(document_.GetScene(), document_.GetHistory(),
		                      kind, selection_.GetActiveId(), error);
		if (!id)
			ShowError(error);
		else {
			selection_.SelectOnly(id);
			hierarchyPanel_.RequestReveal(id);
			inspectorPanel_.RequestInspect(id);
			windowFocusRequest_ = "Game";
		}

		return;
	}

	case EditorAction::CREATE_EMPTY:
		CreateObject(SceneObject{});
		return;
	case EditorAction::CREATE_BOX:
		CreateObject(PrimitiveObjects::CreateBox());
		return;
	case EditorAction::CREATE_SIZED_BOX: {
		try {
			Collider bounds;

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

const Scene *EditorScene::PrepareGameExport(SceneDocument &startup,
                                            GameExportOptions &options,
                                            std::string &error,
                                            std::vector<GameExportIssue> &issues) {
	try {
		const auto *scene = GetStartupSceneForRun(startup, false);
		if (!scene) {
			error = std::format(
			    "Could not load the project startup scene: {}\n{}\n"
			    "Choose a valid scene with Choose Startup Scene.",
			    PathUtils::ToUtf8(project_.GetStartupScene()), startup.GetError());
			issues.push_back({.message = error,
			                  .locationPath = project_.GetStartupScene(),
			                  .field = GameExportField::STARTUP_SCENE});
			return nullptr;
		}
		options.assetDirectories = assetSources_.ForScene(*scene);
		// 配布画面のウィンドウ設定を保ち、入力などは現在のゲーム設定を使う。
		auto config = exportConfig_;
		config.title = std::move(options.config.title);
		config.width = options.config.width;
		config.height = options.config.height;
		options.config = std::move(config);
		options.startupScenePath = scene == &document_.GetScene()
		                               ? document_.GetFilePath()
		                               : startup.GetFilePath();
		options.config.fallbackCamera = scene == &document_.GetScene()
		                                    ? cameraController_.GetPose()
		                                    : exportConfig_.fallbackCamera;
		return scene;
	} catch (const std::exception &issue) {
		error = issue.what();
		return nullptr;
	}
}

void EditorScene::ShowGameExport(const std::filesystem::path &windowSource) {
	try {
		GameExportOptions options;
		const auto root = PathUtils::GetExecutableDirectory();
#if defined(_DEBUG)
		options.runtimeDirectory = root.parent_path() / L"Release";
#else
		options.runtimeDirectory = root;
#endif
		options.resourceDirectory = root / L"resources";
		options.destination = root / L"exports" / L"Game";
		const auto lastExport = preferences_.LastGameExport(project_.GetRoot());
		const auto lastParent = lastExport.parent_path();
		std::error_code statusError;
		if (!lastParent.empty() &&
		    PathUtils::IsValidFileName(lastExport.filename()) &&
		    std::filesystem::is_directory(lastParent, statusError)) {
			options.destination = lastExport;
		}
		options.config = exportConfig_;
		const auto startup = project_.GetStartupScene();
		const bool isCurrentScene = UsesEditingSceneForStartup(startup);
		if (isCurrentScene) {
			options.config.fallbackCamera = cameraController_.GetPose();
		}
		options.startupScenePath =
		    isCurrentScene ? document_.GetFilePath() : startup;
		if (exportDialog_.Show(std::move(options),
		                       isCurrentScene && document_.IsModified(),
		                       project_.GetRoot(), lastExport, windowSource)) {
			CheckGameExport(true);
		}
		stage_ = DialogStage::EXPORT_GAME;
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::PrepareExport: {}", error.what()));
		ShowError(
		    "Could not prepare game export. See the log for details.");
	}
}

void EditorScene::WriteGameExport() {
	SceneDocument startup;
	auto options = exportDialog_.GetOptions();
	std::string error;
	std::filesystem::path directory;
	std::vector<GameExportIssue> issues;
	const auto *scene = PrepareGameExport(startup, options, error, issues);
	if (scene) {
		// 配布出力中は定期記録が進まないため、未保存の編集を先に記録する。
		RecordRecovery();
	}
	if (!scene || !GameExporter::Export(*scene, options, error, &issues, &directory)) {
		exportDialog_.ShowExportFailure("Export failed: " + error, std::move(issues));
		stage_ = DialogStage::EXPORT_GAME;
		return;
	}

	Logger::Info("Game exported to " + PathUtils::ToUtf8(directory));
	if (project_.GetRoot().empty()) {
		exportConfig_ = options.config;
	}
	preferences_.RememberGameExport(project_.GetRoot(), directory);
	exportDialog_.CompleteExport();
	preferences_.gameExportDrafts.erase(project_.GetRoot());
	if (canSavePreferences_) {
		preferences_.Save();
	}
	exportResultDialog_.Show(directory);
	stage_ = DialogStage::EXPORT_RESULT;
}

void EditorScene::CheckGameExport(bool continueFromIssue) {
	SceneDocument startup;
	auto options = exportDialog_.GetOptions();
	std::string error;
	std::vector<GameExportIssue> issues;
	GameExporter::CheckSummary summary;
	const auto *scene = PrepareGameExport(startup, options, error, issues);
	if (scene && GameExporter::Check(*scene, options, error, &issues, &summary)) {
		constexpr double kBytesPerMiB = 1024.0 * 1024.0;
		const auto &includedScenes = summary.includedScenes;
		auto report = std::format("Export check passed. No files were written.\n\n"
		                          "Output folder: {}\n"
		                          "Game window: {} ({} × {})\n"
		                          "Estimated output: {:.1f} MiB ({} files)",
		                          PathUtils::ToUtf8(summary.destination), options.config.title,
		                          options.config.width, options.config.height,
		                          static_cast<double>(summary.contentBytes) / kBytesPerMiB,
		                          summary.fileCount);
		if (summary.availableBytes) {
			report += std::format("\nAvailable at destination: {:.1f} MiB",
			                      static_cast<double>(*summary.availableBytes) / kBytesPerMiB);
		}
		report += std::format("\n\nIncluded scenes ({}):", includedScenes.size());
		for (std::size_t i = 0; i < includedScenes.size(); ++i) {
			const auto &path = includedScenes[i];
			report += std::format("\n{}{}", i == 0 ? "Startup: " : "",
			                      path.empty() ? "Untitled" : PathUtils::ToUtf8(path));
		}
		exportDialog_.SetCheckResult(std::move(report));
		return;
	}

	if (issues.empty()) {
		issues.push_back({error});
	}
	exportDialog_.SetCheckResult("Export check failed: " + error,
	                             std::move(issues), continueFromIssue);
}

void EditorScene::RevealGameExportObject(SceneObjectId object) {
	if (object == 0) {
		return;
	}
	if (document_.GetScene().Contains(object)) {
		RevealReferenceObject(object);
		return;
	}

	const bool canSaveAndCheck =
	    document_.IsModified() && !document_.GetFilePath().empty() &&
	    !UsesEditingSceneForStartup(project_.GetStartupScene());
	CheckGameExport(true);
	exportDialog_.Reopen(
	    canSaveAndCheck
	        ? "This object is absent from the editing scene. Use Save Scene and Check "
	          "to include your edits."
	        : "The reported object is no longer in the scene. The results were refreshed.");
	stage_ = DialogStage::EXPORT_GAME;
}

void EditorScene::BrowseStartupScene(bool repairing) {
	const auto startup = project_.GetStartupScene();
	auto initialPath = GetSceneDialogDirectory(startup);
	std::error_code error;
	if (!startup.empty() &&
	    std::filesystem::is_regular_file(startup, error) &&
	    PathUtils::IsSameFile(initialPath, startup.parent_path())) {
		initialPath /= startup.filename();
	}

	fileDialog_.Show(FileDialogMode::OPEN, initialPath, FileDialogFilter::SCENE,
	                 "Choose Startup Scene###FileDialog");
	assets_.Refresh();
	fileDialog_.SetProjectScenes(project_.GetRoot(), assets_.GetSceneTargets(),
	                             repairing || preferences_.searchProjectScenes);
}

bool EditorScene::SetStartupScene(const std::filesystem::path &path,
                                  std::string &error) {
	if (!project_.CanUseAsStartupScene(path)) {
		error = "Choose a saved scene in the project's scenes folder.";
		return false;
	}

	SceneDocument candidate;
	const auto &config = project_.GetGameConfig();
	if (!candidate.Open(path, &config.lighting)) {
		error = std::format("Could not set the startup scene: {}\n{}",
		                    PathUtils::ToUtf8(path), candidate.GetError());
		return false;
	}

	const auto previous = project_.GetStartupScene();
	const auto &previousSource = previous.empty() ? document_.GetFilePath() : previous;
	if (!project_.Save(config, candidate.GetFilePath(), error)) {
		return false;
	}

	exportDialog_.FollowStartupScene(project_.GetRoot(), previousSource,
	                                 project_.GetStartupScene());
	SaveEditorSession();
	return true;
}

bool EditorScene::OpenGameExport(std::filesystem::path directory) {
	if (!FileDialog::GameExportOpenProblem(directory).empty())
		return false;

	const auto &root = project_.GetRoot();
	const auto previous =
	    std::exchange(pendingGameExportReplacement_, std::filesystem::path{});
	const auto recent = preferences_.RecentGameExports(root);
	if (!previous.empty() || recent.empty() || recent.front() != directory) {
		preferences_.RememberOpenedGameExport(root, directory, previous);
		if (canSavePreferences_)
			preferences_.Save();
	}
	exportResultDialog_.Show(std::move(directory), true);
	stage_ = DialogStage::EXPORT_RESULT;
	return true;
}

void EditorScene::ConfirmSceneChanges() {
	const bool exporting = pendingAction_ == EditorAction::WRITE_GAME_EXPORT;
	if (document_.IsModified() &&
	    (!exporting || !UsesEditingSceneForStartup(project_.GetStartupScene()))) {
		const auto &sceneFile = recoverySourceFile_ ? *recoverySourceFile_
		                                           : document_.GetFilePath();
		auto name = PathUtils::ToUtf8(sceneFile);
		if (name.empty() && !recoverySourceFile_) {
			name = "Untitled";
		}
		std::string destination;
		const char *cancelLabel = "Cancel";
		switch (pendingAction_) {
		case EditorAction::OPEN:
			destination = PathUtils::ToUtf8(openSceneDialog_.GetSelectedPath());
			cancelLabel = "Back to Open Scene";
			break;
		case EditorAction::NEW_PROJECT:
		case EditorAction::OPEN_PROJECT:
			destination = PathUtils::ToUtf8(projectDialog_.GetSelectedPath());
			cancelLabel = "Back to Project Selection";
			break;
		case EditorAction::OPEN_PLAY_SOURCE:
			destination = PathUtils::ToUtf8(pendingOpenPath_);
			cancelLabel = "Back to Play";
			break;
		case EditorAction::OPEN_PATH:
		case EditorAction::OPEN_PROJECT_PATH:
		case EditorAction::OPEN_EXPORT_ISSUE:
			destination = PathUtils::ToUtf8(pendingOpenPath_);
			break;
		default:
			break;
		}
		saveChangesDialog_.Show(std::move(name), exporting,
		                        cancelLabel,
		                        std::move(destination), recoverySourceFile_.has_value());
		stage_ = DialogStage::CONFIRM_SAVE;
	} else {
		ExecutePendingAction();
	}
}

void EditorScene::CancelPendingAction() {
	const auto action = std::exchange(pendingAction_, EditorAction::NONE);
	stage_ = DialogStage::IDLE;
	if (action == EditorAction::OPEN) {
		openSceneDialog_.RetrySelection();
		stage_ = DialogStage::OPEN_FILE;
	} else if (action == EditorAction::NEW_PROJECT || action == EditorAction::OPEN_PROJECT) {
		projectDialog_.RetrySelection();
		stage_ = action == EditorAction::NEW_PROJECT ? DialogStage::CREATE_PROJECT
		                                            : DialogStage::OPEN_PROJECT;
	} else if (action == EditorAction::WRITE_GAME_EXPORT ||
	    action == EditorAction::SAVE_AND_CHECK_EXPORT ||
	    action == EditorAction::OPEN_EXPORT_ISSUE) {
		exportDialog_.Reopen();
		stage_ = DialogStage::EXPORT_GAME;
	}
}

void EditorScene::ExecutePendingAction() {
	const EditorAction action = pendingAction_;

	pendingAction_ = EditorAction::NONE;
	stage_ = DialogStage::IDLE;

	switch (action) {
	case EditorAction::WRITE_GAME_EXPORT:
		WriteGameExport();
		return;
	case EditorAction::SAVE_AND_CHECK_EXPORT:
		CheckGameExport(true);
		exportDialog_.Reopen();
		stage_ = DialogStage::EXPORT_GAME;
		return;

	case EditorAction::OPEN_PROJECT_PATH:
		SwitchProject(pendingOpenPath_, false, true);
		return;

	case EditorAction::NEW_PROJECT:
	case EditorAction::OPEN_PROJECT: {
		stage_ = action == EditorAction::NEW_PROJECT ? DialogStage::CREATE_PROJECT
		                                            : DialogStage::OPEN_PROJECT;
		const auto folder = projectDialog_.GetSelectedPath();
		SwitchProject(folder, action == EditorAction::NEW_PROJECT);
		startupProjectPicker_ = startupProjectPicker_ && stage_ == DialogStage::OPEN_PROJECT;
		return;
	}

	case EditorAction::EXPORT_GAME:
		ShowGameExport();
		return;

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

			SaveEditorSession();
			document_.New();
			if (lastPlayScenePath_.empty()) {
				lastPlayCameraOverride_ = 0;
				lastPlayFollowedEditingScene_ = false;
			}
			if (playFailureReplay_ && playFailureReplay_->path.empty()) {
				playFailureReplay_.reset();
			}
			exportDialog_.ForgetUntitledDraft();
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
			sceneImportDialog_.Show(assets_, assetsPanel_.GetOpenDirectory(),
			                        project_.GetRoot().empty());
			stage_ = DialogStage::IMPORT_SCENE;
		} catch (const std::exception &error) {
			Logger::Error(
			    std::format("EditorScene::ImportScene: {}", error.what()));
			ShowError(
			    "Could not prepare scene import. See the log for details.");
		}

		return;

	case EditorAction::OPEN:
		if (OpenScene(openSceneDialog_.GetSelectedPath(), false, nullptr,
		              locatedScenePreviousPath_)) {
			locatedScenePreviousPath_.clear();
			SaveEditorSession();
		} else {
			openSceneDialog_.RetrySelection(document_.GetError());
			stage_ = DialogStage::OPEN_FILE;
		}
		return;
	case EditorAction::OPEN_PATH:
	case EditorAction::OPEN_PLAY_SOURCE: {
		const auto object = std::exchange(pendingOpenObject_, SceneObjectId{0});
		if (!failedRestoredScene_.empty() && pendingOpenPath_ == failedRestoredScene_) {
			std::error_code status;
			const auto resolved = std::filesystem::canonical(pendingOpenPath_, status);
			if (!status) {
				try {
					PathUtils::RelativeInside(project_.GetRoot(), resolved);
				} catch (const std::runtime_error &) {
					ShowError("The last edited scene points outside the current project.");
					return;
				}
			}
		}
		if (OpenScene(pendingOpenPath_, false)) {
			if (action == EditorAction::OPEN_PLAY_SOURCE) {
				playMode_.Stop();
				ClearPlaySelection();
			}
			SaveEditorSession();
			if (object != 0) {
				RevealReferenceObject(object);
			}
		} else {
			ShowError(std::format("Could not open {}. The current scene was kept.\n{}",
			                      PathUtils::ToUtf8(pendingOpenPath_),
			                      document_.GetError()));
			failedOpenScenePath_ = pendingOpenPath_;
			failedOpenSceneObject_ = object;
		}
		return;
	}
	case EditorAction::OPEN_EXPORT_ISSUE: {
		const auto object = std::exchange(pendingOpenObject_, SceneObjectId{0});
		if (!OpenScene(pendingOpenPath_, false)) {
			exportDialog_.Reopen("Could not open the referenced scene: " +
			                     document_.GetError());
			stage_ = DialogStage::EXPORT_GAME;
			return;
		}
		SaveEditorSession();
		RevealGameExportObject(object);
		return;
	}
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
			SaveEditorSession();
			ClearRecovery();
			assets_.Refresh();
			ExecutePendingAction();
		} else {
			ShowSceneSaveError();
		}

		return;
	}

	const auto &current = document_.GetFilePath();
	const auto directory = GetSceneDialogDirectory(current);
	auto path = directory / current.filename();
	if (recoverySourceFile_) {
		path = GetRecoveredScenePath();
	} else if (current.empty()) {
		path = directory / L"Untitled.scene.json";
		try {
			const auto parent = PathUtils::ResolveFromExecutable(path).parent_path();
			path = parent / AssetFolders::SuggestAvailableName(parent, L"Untitled", L".scene.json");
		} catch (const std::filesystem::filesystem_error &) {
			// 初期フォルダを確認できない場合も、保存先を手動で選べるようにする。
		}
	}
	fileDialog_.Show(FileDialogMode::SAVE, path);
	if (current.empty()) {
		fileDialog_.RequireNewFile();
	}

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
			Collider fitted;

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

		Collider collider;

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
                                    Collider &collider) {
	Collider fitted = object.collider.value_or(Collider{});

	if (!object.HasMesh()) {
		fitted.center = {};
		fitted.halfExtents = {.5f, .5f, .5f};
		fitted.radius = .5f;
		fitted.halfHeight = .5f;
	}

	if (object.HasMesh()) {
		const auto model = models_.Load(object.mesh->modelPath);
		std::array<DirectX::XMFLOAT3, 8> corners;

		if (!models_.GetLocalBounds(model, corners) ||
		    !ColliderUtils::FitToPoints(corners, fitted)) {
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

			std::optional<Collider> collider;

			if (operation != ColliderOperation::REMOVE_ALL) {
				Collider fitted;

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

		SceneWorldTransform world;
		if (!SceneHierarchy::GetWorldTransform(document_.GetScene(),
		                                       selection_.GetActiveId(), world))
			return;
		center = world.GetPosition();
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
EditorScene::GetSceneDialogDirectory(const std::filesystem::path &scene) const {
	std::error_code error;
	if (project_.GetRoot().empty()) {
		const auto folder = scene.parent_path();
		if (std::filesystem::is_directory(folder, error)) {
			return folder;
		}

		const auto &assets = assetSources_.GetDirectory();
		return assets.is_absolute() && std::filesystem::is_directory(assets, error)
		           ? assets
		           : std::filesystem::path{};
	}

	const auto scenes = project_.GetScenesDirectory();
	if (!std::filesystem::is_directory(scenes, error)) {
		return std::filesystem::is_directory(project_.GetRoot(), error)
		           ? project_.GetRoot()
		           : std::filesystem::path{};
	}

	if (scene.empty()) {
		return scenes;
	}

	try {
		const auto root = std::filesystem::canonical(scenes);
		const auto folder = std::filesystem::weakly_canonical(scene.parent_path());
		for (auto relative = PathUtils::RelativeInside(root, folder);
		     !relative.empty(); relative = relative.parent_path()) {
			if (std::filesystem::is_directory(root / relative, error)) {
				return root / relative;
			}
		}
	} catch (const std::runtime_error &) {
		// プロジェクト外のシーンや解決できない場所では既定の一覧を開く。
	}
	return scenes;
}

std::filesystem::path EditorScene::GetRecoveredScenePath() const {
	const auto &source = recoverySourceFile_.value();
	auto stem = source.empty() ? std::filesystem::path(L"Untitled") : source.stem();
	if (PathUtils::HasExtension(stem, ".scene")) {
		stem = stem.stem();
	}
	const auto directory = GetSceneDialogDirectory(source);
	auto base = stem.native() + L".recovered";
	const std::wstring extension = L".scene.json";
	if (!PathUtils::IsValidFileName(base + extension)) {
		base = L"Recovered";
	}

	try {
		const auto parent = PathUtils::ResolveFromExecutable(directory / (base + extension)).parent_path();
		try {
			return parent / AssetFolders::SuggestAvailableName(parent, base, extension);
		} catch (const std::invalid_argument &) {
			// 元の名前では連番を付けられない場合、短い名前で探す。
			return parent / AssetFolders::SuggestAvailableName(parent, L"Recovered", extension);
		}
	} catch (const std::filesystem::filesystem_error &) {
		// フォルダを検査できない場合は、保存画面で別の場所を選べるようにする。
		return directory / (base + extension);
	}
}

std::filesystem::path EditorScene::GetSceneCopyPath() const {
	const auto &currentPath = document_.GetFilePath();
	if (currentPath.empty() && recoverySourceFile_) {
		return GetRecoveredScenePath();
	}

	auto stem = currentPath.empty() ? std::filesystem::path(L"Untitled")
	                               : currentPath.stem();
	if (PathUtils::HasExtension(stem, ".scene")) {
		stem = stem.stem();
	}
	const auto directory = GetSceneDialogDirectory(currentPath);
	const auto searchDirectory = directory.empty()
	                                 ? PathUtils::GetExecutableDirectory()
	                                 : directory;
	auto copyStem = stem.native() + L".copy";
	const std::wstring extension = L".scene.json";
	if (!PathUtils::IsValidFileName(copyStem + extension)) {
		copyStem = L"Scene.copy";
	}

	try {
		return searchDirectory / AssetFolders::SuggestAvailableName(
		                             searchDirectory, copyStem, extension);
	} catch (const std::filesystem::filesystem_error &) {
		// 元の場所を確認できない場合も、保存画面から別の場所を選べるようにする。
		return PathUtils::GetExecutableDirectory() / (copyStem + extension);
	}
}

void EditorScene::StartSaveCopy(bool reload) {
	try {
		fileDialog_.Show(FileDialogMode::SAVE_COPY, GetSceneCopyPath());
		if (reload || recoverySourceFile_) {
			fileDialog_.RequireNewFile();
		}
		stage_ = reload ? DialogStage::SAVE_COPY_AND_RELOAD
		                : DialogStage::SAVE_COPY_FILE;
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::StartSaveCopy: {}", error.what()));
		ShowError(
		    "Could not select a copy destination. See the log for details.");
	}
}

void EditorScene::ReloadScene(const std::filesystem::path &copy) {
	const auto original = document_.GetFilePath();
	const auto action = pendingAction_;
	if (OpenScene(original, false)) {
		stage_ = DialogStage::IDLE;
		SaveEditorSession();
		if (action != EditorAction::NONE) {
			ExecutePendingAction();
		}
	} else {
		auto message = copy.empty() ? std::string{}
		                           : std::format("Saved a copy to {}. ", PathUtils::ToUtf8(copy));
		message += std::format("Could not reload {}. The editing scene was kept.\n{}",
		                       PathUtils::ToUtf8(original), document_.GetError());
		ShowError(std::move(message));
		failedOpenScenePath_ = original;
		reloadBackupPath_ = copy;
		sceneReloadFailed_ = true;
		pendingAction_ = action;
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
		if (recoverySourceFile_) {
			fileDialog_.RequireNewFile();
		}
		stage_ = DialogStage::SAVE_SELECTION_FILE;
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::SaveSelection: {}", error.what()));
		ShowError(
		    "Could not prepare a selection copy. See the log for details.");
	}
}

void EditorScene::AppendAssetObjects(const std::filesystem::path &path) {
	if (playMode_.IsPlaying() || IsBusy() || gizmo_.IsUsing() ||
	    gizmoEditActive_ || sceneViewPanel_.IsSelecting()) {
		return;
	}

	try {
		SceneDocument source;
		const bool prefab = PrefabAsset::IsPrefabPath(path);
		std::string error;

		if (prefab ? !PrefabAsset::Load(path, source.GetScene(), error)
		           : !source.Open(path, nullptr)) {
			ShowError(error.empty() ? "Could not read the asset objects. The "
			                          "current scene was kept."
			                        : error);
			return;
		}

		auto sources = assetSources_;

		if (prefab) {
			sources.Register(
			    PathUtils::ResolveFromExecutable(assets_.GetRootDirectory()));
		}

		auto ids =
		    prefab ? PrefabInstances::Instantiate(path, source.GetScene(),
		                                          document_.GetHistory(), error)
		           : SceneObjects::Append(source.GetScene(),
		                                  document_.GetHistory());
		if (ids.empty()) {
			ShowError(error.empty() ? "The asset has no objects to add, or its "
			                          "settings are invalid."
			                        : error);
			return;
		}

		assetSources_ = std::move(sources);
		selection_.Set(std::move(ids));
		hierarchyPanel_.RequestReveal(selection_.GetActiveId());
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::AppendAssetObjects: {}", error.what()));
		ShowError(
		    "Could not add objects from the scene. See the log for details.");
	}
}

bool EditorScene::PlaceDroppedObjects(const std::filesystem::path &path,
                                      const Ray &ray) {
	if (playMode_.IsPlaying() || IsBusy() || gizmo_.IsUsing() ||
	    gizmoEditActive_ || sceneViewPanel_.IsSelecting()) {
		return false;
	}

	try {
		SceneDocument source;
		const bool prefab = PrefabAsset::IsPrefabPath(path);
		std::string error;
		const bool loaded =
		    prefab ? PrefabAsset::Load(path, source.GetScene(), error)
		           : source.Open(path, nullptr);
		auto sources = assetSources_;

		if (prefab) {
			sources.Register(
			    PathUtils::ResolveFromExecutable(assets_.GetRootDirectory()));
		}

		if (!loaded ||
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
		    prefab ? PrefabInstances::Instantiate(path, source.GetScene(),
		                                          document_.GetHistory(), error)
		           : SceneObjects::Append(source.GetScene(),
		                                  document_.GetHistory());
		if (ids.empty()) {
			ShowError(error.empty() ? "Could not add the positioned objects. "
			                          "No objects were added."
			                        : error);
			return false;
		}

		assetSources_ = std::move(sources);
		selection_.Set(std::move(ids));
		hierarchyPanel_.RequestReveal(selection_.GetActiveId());
		return true;
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::PlaceDroppedObjects: {}", error.what()));
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

	if (!document_.IsModified() || !recovery_.IsReady()) {
		return;
	}

	std::string error;

	const auto &sourceFile = recoverySourceFile_ ? *recoverySourceFile_
	                                             : document_.GetFilePath();
	const auto view = cameraController_.GetViewState();
	if (!recovery_.Record(document_.GetScene(), sourceFile,
	                      assetSources_, exportConfig_, error, &view, selection_.GetIds())) {
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
	recoverySourceFile_.reset();
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

void EditorScene::ShowRecovery(bool automatic) {
	std::vector<SceneRecovery::Entry> entries;
	std::string error;
	const auto projectAssets = project_.GetRoot().empty()
	                               ? std::filesystem::path{}
	                               : project_.GetAssetsDirectory();
	const bool enumerated = recovery_.Enumerate(entries, error);
	if (automatic) {
		if (!enumerated) {
			Logger::Error("EditorScene::Recovery: " + error);
			return;
		}
		if (std::ranges::none_of(entries, [&](const auto &entry) {
			    return entry.CanRestore(projectAssets);
		    })) {
			return;
		}
	}
	recoveryDialog_.Show(std::move(entries), projectAssets, std::move(error));
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

		if (!project_.GetRoot().empty() &&
		    !PathUtils::IsSameFile(project_.GetAssetsDirectory(),
		                           snapshot.assets.GetDirectory())) {
			ShowError("This recovery belongs to a different Assets folder.");
			return;
		}

		AssetDatabase assets(snapshot.assets.GetDirectory(),
		                     project_.GetRoot().empty()
		                         ? std::filesystem::path{}
		                         : project_.GetScenesDirectory());
		const bool assetsReady = assets.Refresh();
		const auto previousAssetFolder =
		    PathUtils::IsSameFile(assets_.GetRootDirectory(), assets.GetRootDirectory())
		        ? assetsPanel_.GetOpenDirectory() : std::filesystem::path{};
		auto restoredPath = pendingRecoveryPath_;
		SaveEditorSession();
		SceneLighting::MigrateLegacyDirectionalLight(
		    snapshot.scene, project_.GetRoot().empty() ? snapshot.game.lighting
		                                               : exportConfig_.lighting);
		document_.GetScene().CopyFrom(snapshot.scene);
		document_.SetUntitled();
		if (lastPlayScenePath_.empty()) {
			lastPlayCameraOverride_ = 0;
			lastPlayFollowedEditingScene_ = false;
		}
		if (playFailureReplay_ && playFailureReplay_->path.empty()) {
			playFailureReplay_.reset();
		}
		exportDialog_.ForgetUntitledDraft();
		ClearRecovery(pendingRecoveryPath_);
		recoverySourceFile_ = std::move(snapshot.sourceFile);
		assetSources_ = std::move(snapshot.assets);
		if (project_.GetRoot().empty()) {
			exportConfig_ = std::move(snapshot.game);
		}
		assets_ = std::move(assets);
		suspendedRecyclePaths_.clear();
		restoredRecoveryPath_.swap(restoredPath);
		sceneCheckPanel_.Reset();
		clipboard_.DetachExternalReferences();
		frameRequest_ = FrameRequest::NONE;
		frameObject_ = 0;
		gizmo_.Cancel();
		gizmoEditActive_ = false;
		RestoreSceneViewAndSelection(*recoverySourceFile_, &snapshot);
		pendingPlayCamera_ = 0;
		playMode_.SetPreviewCamera(0);
		inspectorPanel_.Clear();
		assetsPanel_.Clear();
		assetsPanel_.RestoreDirectory(previousAssetFolder, assets_);
		stage_ = DialogStage::IDLE;
		RecordRecovery();
		if (!assetsReady) {
			Logger::Warning("Scene restored, but the Assets list could not be loaded: " +
			                    assets_.GetError(),
			                assetSources_.GetDirectory());
			consolePanel_.Show();
		}
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("EditorScene::RestoreRecovery: {}", error.what()));
		ShowError("Could not restore the scene. See the log for details.");
	}
}

std::vector<std::filesystem::path>
EditorScene::CheckAssetReferences(bool recycle) {
	assetReferenceLocationError_.clear();
	confirmClearAssetUndoHistory_ = false;
	try {
		const auto &assets = assets_.GetRootDirectory();

		if (!assets.is_absolute() || pendingAssetPaths_.empty()) {
			throw std::runtime_error("Choose items inside editable Assets.");
		}

		const auto root = std::filesystem::canonical(assets);
		for (const auto &path : pendingAssetPaths_) {
			const auto status = std::filesystem::symlink_status(path);

			if (!path.is_absolute() ||
			    (!std::filesystem::is_regular_file(status) &&
			     !std::filesystem::is_directory(status))) {
				throw std::runtime_error("Choose a file or folder inside Assets.");
			}

			auto target = std::filesystem::canonical(path);
			const auto relative = PathUtils::RelativeInside(root, target);

			if (relative.empty() ||
			    PathUtils::RelativeInside(root, path.lexically_normal()) != relative) {
				throw std::runtime_error(
				    "Choose an item inside Assets without using a redirected path.");
			}

		}

		const auto &project = project_.GetRoot();
		AssetUsage::Paths targets;
		assetReferences_ = AssetUsage::Find(
		    pendingAssetPaths_, project.empty() ? root : project,
		    document_.GetScene(), document_.GetFilePath(), project_.GetStartupScene(),
		    recycle ? &targets : nullptr, &document_.GetHistory());
		return targets;
	} catch (const std::exception &error) {
		assetReferences_ = {{std::string("Could not verify asset references.\n") +
		                     error.what()}};
		return {};
	}
}

void EditorScene::DrawAssetReferences(bool recycle) {
	const char *title =
	    recycle ? "Move Asset to Recycle Bin" : "Asset References";

	if (!ImGui::IsPopupOpen(title)) {
		ui_.OpenPopup(title);
	}

	if (!ui_.BeginPopupModal(title)) {
		return;
	}

	ui_.Text(std::format("{} item(s) selected", pendingAssetPaths_.size()));
	ui_.SameLine();
	if (ui_.Button("Refresh References")) {
		CheckAssetReferences(recycle);
	}
	if (recycle && !assetReferences_.empty()) {
		const auto missing = [](const auto &path) {
			std::error_code status;
			return std::filesystem::symlink_status(path, status).type() ==
			       std::filesystem::file_type::not_found;
		};
		if (std::ranges::any_of(pendingAssetPaths_, missing)) {
			ui_.SameLine();
			if (ui_.Button("Remove Missing Items and Recheck")) {
				std::erase_if(pendingAssetPaths_, missing);
				if (pendingAssetPaths_.empty()) {
					assetReferences_.clear();
					ui_.CloseCurrentPopup();
					ui_.EndPopup();
					stage_ = DialogStage::IDLE;
					return;
				}
				CheckAssetReferences(true);
			}
		}
	}

	if (ui_.BeginChild("RecycleSelection", 500.0f, 90.0f)) {
		for (const auto &path : pendingAssetPaths_) {
			ui_.Text(PathUtils::ToUtf8(
			    path.lexically_relative(assets_.GetRootDirectory())));
		}
	}

	ui_.EndChild();

	std::optional<AssetUsage::Reference> destination;
	std::filesystem::path sourceToReveal;
	std::vector<SceneObjectId> selectedReferences;
	if (!assetReferences_.empty()) {
		ui_.Text(recycle ? "The selection could not be moved. References or errors:"
		                 : "External references or items that could not be checked:");
		ui_.InputText("Search References", assetReferenceFilter_);
		ImGui::SetItemTooltip("Search reference details or paste a source path.");
		ui_.SameLine();
		ui_.BeginDisabled(assetReferenceFilter_.empty());
		if (ui_.Button("Clear Search")) {
			assetReferenceFilter_.clear();
		}
		ui_.EndDisabled();
		const auto searchPath = PathUtils::FromPathSearch(assetReferenceFilter_);
		const auto matches = [&](const AssetUsage::Reference &source) {
			return source.message.find(assetReferenceFilter_) != std::string::npos ||
			       PathUtils::MatchesPathSearch(source.scenePath, searchPath) ||
			       PathUtils::MatchesPathSearch(source.sourcePath, searchPath) ||
			       (source.editingScene &&
			        PathUtils::MatchesPathSearch(document_.GetFilePath(), searchPath));
		};
		const auto matchingCount = std::ranges::count_if(assetReferences_, matches);
		ui_.Text(std::format("Matching references: {} / {}", matchingCount,
		                     assetReferences_.size()));

		if (ui_.BeginChild("AssetReferences", 500.0f, 160.0f)) {
			if (matchingCount == 0) {
				ui_.Text("No references match the search.");
			}
			for (std::size_t i = 0; i < assetReferences_.size(); ++i) {
				const auto &source = assetReferences_[i];
				if (!matches(source)) {
					continue;
				}
				ImGui::TextWrapped("%s", source.message.c_str());
				if (recycle && source.startupScene && ui_.Button("Choose Startup Scene...")) {
					ui_.EndChild();
					ui_.CloseCurrentPopup();
					ui_.EndPopup();
					BrowseStartupScene();
					stage_ = DialogStage::SELECT_RECYCLE_STARTUP_SCENE;
					return;
				}
				if (source.editingScene || !source.scenePath.empty()) {
					ImGui::PushID(static_cast<int>(i));
					if (ui_.Button(source.editingScene ? "Show Object"
					                                  : "Open Scene and Show Object")) {
						destination = source;
					}
					ImGui::PopID();
				}
				if (!source.sourcePath.empty()) {
					ImGui::PushID(static_cast<int>(i));
					if (ui_.Button("Show Source in Assets")) {
						sourceToReveal = source.sourcePath;
					}
					ui_.SameLine();
					if (ui_.Button("Show Source in Explorer")) {
						ShellUtils::RevealLocationInExplorer(source.sourcePath,
						                                     assetReferenceLocationError_);
					}
					ImGui::SetItemTooltip("%s", PathUtils::ToUtf8(source.sourcePath).c_str());
					ImGui::PopID();
				}
			}
		}

		ui_.EndChild();
		const auto copyReferences = [&](bool matchingOnly) {
			std::string list;
			for (const auto &source : assetReferences_) {
				if (matchingOnly && !matches(source)) {
					continue;
				}
				list += source.message;
				list += '\n';
				const auto &path = source.editingScene ? document_.GetFilePath()
				                   : source.scenePath.empty() ? source.sourcePath
				                                              : source.scenePath;
				if (!path.empty()) {
					list += std::format("Source: {}\n", PathUtils::ToUtf8(path));
				}
			}
			ImGui::SetClipboardText(list.c_str());
		};
		if (ui_.Button("Copy List")) {
			copyReferences(false);
		}
		ImGui::SetItemTooltip("Copy the full reference list, including entries hidden by search.");
		if (!assetReferenceFilter_.empty()) {
			ui_.SameLine();
			ui_.BeginDisabled(matchingCount == 0);
			if (ui_.Button("Copy Matching References") && matchingCount != 0) {
				copyReferences(true);
			}
			ui_.EndDisabled();
		}
		ui_.SameLine();
		const auto &scene = document_.GetScene();
		const auto inCurrentScene = [&](const AssetUsage::Reference &source) {
			return matches(source) && source.editingScene && scene.Contains(source.objectId);
		};
		ui_.BeginDisabled(!std::ranges::any_of(assetReferences_, inCurrentScene));
		if (ui_.Button("Select Objects in Current Scene")) {
			for (const auto &source : assetReferences_) {
				if (inCurrentScene(source)) {
					selectedReferences.push_back(source.objectId);
				}
			}
			std::ranges::sort(selectedReferences);
			const auto duplicates = std::ranges::unique(selectedReferences);
			selectedReferences.erase(duplicates.begin(), duplicates.end());
		}
		ui_.EndDisabled();
		ImGui::SetItemTooltip("Select objects in the current scene that match the search.");
		ui_.SameLine();
	} else if (!recycle) {
		ui_.Text("No external references found.");
	}
	if (!assetReferenceLocationError_.empty()) {
		ImGui::TextWrapped("%s", assetReferenceLocationError_.c_str());
	}
	const bool historyOnly = recycle && !assetReferences_.empty() &&
	                         std::ranges::all_of(assetReferences_,
	                                             &AssetUsage::Reference::undoHistory);
	if (historyOnly) {
		if (confirmClearAssetUndoHistory_) {
			ui_.Text("Discard all Undo and Redo operations for the current scene?");
			if (ui_.Button("Discard Undo and Redo History")) {
				auto &history = document_.GetHistory();
				const bool modified = history.IsModified();
				history.Reset();
				if (modified) {
					history.Invalidate();
				}
				CheckAssetReferences(true);
			}
			ui_.SameLine();
			if (ui_.Button("Keep History")) {
				confirmClearAssetUndoHistory_ = false;
			}
		} else if (ui_.Button("Clear Scene Undo History...")) {
			confirmClearAssetUndoHistory_ = true;
		}
		ui_.SameLine();
	}

	bool close = false;

	if (recycle && assetReferences_.empty() &&
	    ui_.Button("Move to Recycle Bin")) {
		auto targets = CheckAssetReferences(true);

		if (!targets.empty() && assetReferences_.empty()) {
			std::size_t completed = 0;
			std::string error;

			for (const auto &target : targets) {
				if (!ShellUtils::RecycleAsset(target, error)) {
					assetReferences_ = {{
					    std::format("{} of {} items moved.\n{}\n{}", completed,
					                targets.size(), PathUtils::ToUtf8(target), error)}};
					break;
				}
				++completed;
			}

			if (completed != 0) {
				texturePreviewPanel_.Clear();
				audioPreviewPanel_.Clear();
				assets_.Refresh();
				assetsPanel_.RemoveMissingPaths();
			}

			close = completed == targets.size();
			if (!close && completed != 0) {
				// 移動済みの項目は除き、残りだけを次の参照検査の対象にする。
				targets.erase(targets.begin(), targets.begin() + completed);
				pendingAssetPaths_ = std::move(targets);
			}
		}

		ui_.SameLine();
	}

	const bool canRecycle = recycle && assetReferences_.empty();
	const bool navigating = destination || !sourceToReveal.empty() ||
	                        !selectedReferences.empty();
	if (ui_.Button(canRecycle ? "Cancel" : "Close") ||
	    ImGui::IsKeyPressed(ImGuiKey_Escape) || close || navigating) {
		ui_.CloseCurrentPopup();
		if (recycle && navigating) {
			suspendedRecyclePaths_ = std::move(pendingAssetPaths_);
			suspendedRecycleFilter_ = std::move(assetReferenceFilter_);
		}
		pendingAssetPaths_.clear();
		assetReferences_.clear();
		stage_ = DialogStage::IDLE;
	}

	ui_.EndPopup();

	if (!sourceToReveal.empty()) {
		assetsPanel_.RequestReveal(sourceToReveal, true);
	} else if (!selectedReferences.empty()) {
		if (selection_.Set(std::move(selectedReferences))) {
			inspectorPanel_.Clear();
			inspectorPanel_.RequestInspect(selection_.GetActiveId());
			hierarchyPanel_.RequestReveal(selection_.GetActiveId());
		}
	} else if (destination) {
		if (destination->editingScene) {
			RevealReferenceObject(destination->objectId);
		} else {
			pendingOpenPath_ = std::move(destination->scenePath);
			StartAction(EditorAction::OPEN_PATH, destination->objectId);
		}
	}
}

void EditorScene::RevealReferenceObject(SceneObjectId id) {
	if (!document_.GetScene().Contains(id)) {
		ShowError("The referencing object no longer exists in this scene.");
		return;
	}

	try {
		selection_.SelectOnly(id);
		inspectorPanel_.Clear();
		inspectorPanel_.RequestInspect(id);
		hierarchyPanel_.RequestReveal(id);
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "EditorScene::RevealReferenceObject: {}", error.what()));
		ShowError("Could not select the referencing object.");
	}
}

bool EditorScene::CopyAssets(std::span<const std::filesystem::path> sources,
                             const std::filesystem::path &directory,
                             std::string &error) {
	const auto &root = assets_.GetRootDirectory();
	auto copies = directory.empty()
	                        ? AssetUsage::Duplicate(sources, root, error)
	                        : AssetUsage::CopyTo(sources, directory, root,
	                                             project_.GetRoot(), error);

	if (copies.empty()) {
		return false;
	}

	assets_.Refresh();
	assetsPanel_.RequestSelectResults(std::move(copies));

	return true;
}

bool EditorScene::RelocateAssets(const AssetUsage::Moves &moves,
                                 std::string &error) {
	if (!AssetUsage::Relocate(moves, assets_.GetRootDirectory(), document_,
	                          project_, error)) {
		return false;
	}
	assetsPanel_.RelocatePaths(moves, assets_.GetRootDirectory());
	for (auto &path : suspendedRecyclePaths_) {
		path = PathUtils::AfterMove(path, moves);
	}
	sceneCheckPanel_.OnAssetsMoved(moves);
	exportDialog_.FollowMovedScene(project_.GetRoot(), moves);
	failedRestoredScene_ = PathUtils::AfterMove(failedRestoredScene_, moves);
	lastPlayScenePath_ = PathUtils::AfterMove(lastPlayScenePath_, moves);
	if (playFailureReplay_) {
		playFailureReplay_->path = PathUtils::AfterMove(playFailureReplay_->path, moves);
		playFailureReplay_->scriptPath = PathUtils::AfterMove(playFailureReplay_->scriptPath, moves);
	}

	clipboard_.Clear();
	texturePreviewPanel_.Clear();
	audioPreviewPanel_.Clear();
	assets_.Refresh();
	std::vector<std::filesystem::path> destinations;
	destinations.reserve(moves.size());
	for (const auto &move : moves) {
		destinations.push_back(move.second);
	}
	assetsPanel_.RequestSelectResults(std::move(destinations));

	auto updatedPreferences = preferences_;
	const auto recent = recentScenes_.GetPaths();
	updatedPreferences.recentScenes.assign(recent.begin(), recent.end());
	updatedPreferences.RelocateScenes(moves);
	recentScenes_.Clear();
	for (auto it = updatedPreferences.recentScenes.rbegin();
	     it != updatedPreferences.recentScenes.rend(); ++it) {
		recentScenes_.Add(*it);
	}
	preferences_ = std::move(updatedPreferences);
	SaveEditorSession(true);
	RecordRecovery();
	return true;
}

void EditorScene::DrawAssetRename() {
	constexpr const char *kTitle = "Rename Asset";

	if (!ImGui::IsPopupOpen(kTitle)) {
		ui_.OpenPopup(kTitle);
	}

	if (!ui_.BeginPopupModal(kTitle)) {
		return;
	}

	std::error_code typeError;
	ui_.InputText(std::filesystem::is_directory(pendingRenamePath_, typeError)
	                  ? "Folder Name"
	                  : "File Name",
	              assetRenameName_);

	if (!assetRenameError_.empty() &&
	    ui_.BeginChild("AssetRenameErrors", 500.0f, 140.0f)) {
		ImGui::TextWrapped("%s", assetRenameError_.c_str());
	}

	if (!assetRenameError_.empty()) {
		ui_.EndChild();
	}

	bool close = false;

	if (ui_.Button("Rename")) {
		try {
			const auto name = PathUtils::FromUtf8(assetRenameName_);

			if (!PathUtils::IsValidFileName(name)) {
				throw std::runtime_error("Enter a single valid file name.");
			}

			const auto target = pendingRenamePath_.parent_path() / name;
			close = RelocateAssets({{pendingRenamePath_, target}}, assetRenameError_);
		} catch (const std::exception &error) {
			assetRenameError_ = error.what();
		}
	}

	ui_.SameLine();

	if (ui_.Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape) || close) {
		ui_.CloseCurrentPopup();
		pendingRenamePath_.clear();
		stage_ = DialogStage::IDLE;
	}

	ui_.EndPopup();
}

void EditorScene::UpdateDialogs() {
	switch (stage_) {
	case DialogStage::IDLE:
		return;

	case DialogStage::RECYCLE_ASSET:
	case DialogStage::ASSET_REFERENCES:
		DrawAssetReferences(stage_ == DialogStage::RECYCLE_ASSET);
		return;

	case DialogStage::RENAME_ASSET:
		DrawAssetRename();
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
			CancelPendingAction();
			break;

		case SaveChangesResult::NONE:
			break;
		}

		return;
	}

	case DialogStage::CREATE_PROJECT:
	case DialogStage::OPEN_PROJECT: {
		const auto result = projectDialog_.Draw(ui_);

		if (result == FileDialogResult::CANCELLED) {
			missingRecentProject_.clear();
			stage_ = DialogStage::IDLE;
			if (std::exchange(startupProjectPicker_, false) && recovery_.IsReady()) {
				ShowRecovery(true);
			}
		} else if (result == FileDialogResult::ACCEPTED) {
			const bool creating = stage_ == DialogStage::CREATE_PROJECT;
			const auto &current = project_.GetRoot();
			if (!creating && !current.empty() &&
			    PathUtils::IsSameFile(current, projectDialog_.GetSelectedPath())) {
				missingRecentProject_.clear();
				stage_ = DialogStage::IDLE;
				return;
			}
			pendingAction_ = creating ? EditorAction::NEW_PROJECT : EditorAction::OPEN_PROJECT;
			ConfirmSceneChanges();
		}

		return;
	}

	case DialogStage::OPEN_FILE: {
		const auto result = openSceneDialog_.Draw(ui_);
		if (const auto search = openSceneDialog_.TakeProjectSearchChange()) {
			preferences_.searchProjectScenes = *search;
			SaveEditorSession(true);
		}

		if (result == FileDialogResult::CANCELLED) {
			locatedScenePreviousPath_.clear();
			stage_ = DialogStage::IDLE;
			return;
		}

		if (result == FileDialogResult::ACCEPTED) {
			const auto &selected = openSceneDialog_.GetSelectedPath();
			const bool locating = !locatedScenePreviousPath_.empty();
			if (locating && !project_.CanUseAsStartupScene(selected)) {
				openSceneDialog_.RetrySelection(
				    "Choose a saved scene in the project's scenes folder.");
				return;
			}

			const auto &current = document_.GetFilePath();
			if (!current.empty() && PathUtils::IsSameFile(selected, current)) {
				locatedScenePreviousPath_.clear();
				stage_ = DialogStage::IDLE;
				if (locating) {
					StartAction(EditorAction::RESUME_CURRENT_SCENE);
				}
				return;
			}

			pendingAction_ = EditorAction::OPEN;
			ConfirmSceneChanges();
		}

		return;
	}

	case DialogStage::SAVE_FILE:
	case DialogStage::SAVE_FILE_AFTER_ERROR: {
		const bool retryingSave = stage_ == DialogStage::SAVE_FILE_AFTER_ERROR;
		const auto result = fileDialog_.Draw(ui_);

		if (result == FileDialogResult::CANCELLED) {
			if (retryingSave) {
				errorOpenRequested_ = true;
				stage_ = DialogStage::ERROR;
			} else {
				// 通常の保存先選択を取り消した場合は、NewやExitも取り消す。
				CancelPendingAction();
			}
			return;
		}

		if (result == FileDialogResult::ACCEPTED) {
			const auto previousPath = document_.GetFilePath();
			const auto startup = project_.GetStartupScene();
			SaveEditorSession();
			if (document_.SaveAs(fileDialog_.GetSelectedPath(),
			                     !retryingSave && !previousPath.empty())) {
				if (!failedRestoredScene_.empty() &&
				    project_.CanUseAsStartupScene(document_.GetFilePath())) {
					failedRestoredScene_.clear();
				}
				if (previousPath != document_.GetFilePath()) {
					sceneCheckPanel_.OnSceneSavedAs(previousPath, document_.GetFilePath());
				}
				if (playFailureReplay_ && playFailureReplay_->followsEditingScene &&
				    playFailureReplay_->path == previousPath) {
					playFailureReplay_->path = document_.GetFilePath();
				}
				if (lastPlayFollowedEditingScene_ &&
				    lastPlayScenePath_ == previousPath) {
					lastPlayScenePath_ = document_.GetFilePath();
				}
				if (startup.empty()) {
					exportDialog_.FollowSavedScene(project_.GetRoot(), previousPath,
					                              document_.GetFilePath());
				}
				if (!startup.empty() && !previousPath.empty() &&
				    PathUtils::IsSameFile(previousPath, startup) &&
				    !PathUtils::IsSameFile(document_.GetFilePath(), startup)) {
					const bool canUseSavedScene =
					    project_.CanUseAsStartupScene(document_.GetFilePath());
					Logger::Info(std::format(
					    "Project startup scene: {}. {}", PathUtils::ToUtf8(startup),
					    canUseSavedScene
					        ? "To use the newly saved scene, choose File > Set Current Scene as Startup."
					        : "Save the new scene in the project's scenes folder to use it as startup."));
				}
				recentScenes_.Add(document_.GetFilePath());
				SaveEditorSession();
				ClearRecovery();
				assets_.Refresh();
				const bool continuingExport =
				    pendingAction_ == EditorAction::WRITE_GAME_EXPORT ||
				    pendingAction_ == EditorAction::SAVE_AND_CHECK_EXPORT;
				if (continuingExport && !startup.empty() &&
				    !PathUtils::IsSameFile(startup, document_.GetFilePath()) &&
				    (previousPath.empty() ||
				     !PathUtils::IsSameFile(previousPath, document_.GetFilePath()))) {
					pendingAction_ = EditorAction::NONE;
					CheckGameExport(true);
					exportDialog_.Reopen(std::format(
					    "Saved a separate scene to {}. Export still starts from {} "
					    "and follows the saved scene links. Review the check before exporting.",
					    PathUtils::ToUtf8(document_.GetFilePath()), PathUtils::ToUtf8(startup)));
					stage_ = DialogStage::EXPORT_GAME;
					return;
				}
				ExecutePendingAction();
			} else {
				fileDialog_.RetrySelection(
				    document_.HasSaveConflict()
				        ? "The scene changed outside the editor. Choose a different "
				          "file name to preserve both versions."
				        : document_.GetError());
			}
		}

		return;
	}

	case DialogStage::CONFIRM_RELOAD: {
		const auto result = sceneReloadDialog_.Draw(ui_);

		if (result == SceneReloadResult::CANCELLED) {
			stage_ = DialogStage::IDLE;
		} else if (result == SceneReloadResult::RELOAD) {
			ReloadScene();
		} else if (result == SceneReloadResult::SAVE_COPY_AND_RELOAD) {
			StartSaveCopy(true);
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
			                       selectionSnapshot_, !recoverySourceFile_)) {
				assets_.Refresh();
				assetsPanel_.RequestReveal(fileDialog_.GetSelectedPath());
				selectionSnapshot_.Clear();
				stage_ = DialogStage::IDLE;
			} else {
				fileDialog_.RetrySelection(document_.GetError());
			}
		}

		return;
	}

	case DialogStage::SAVE_COPY_FILE:
	case DialogStage::SAVE_COPY_AND_RELOAD:
	case DialogStage::SAVE_CONFLICT_COPY:
	case DialogStage::SAVE_CONFLICT_COPY_AND_RELOAD: {
		const bool conflictCopy = stage_ == DialogStage::SAVE_CONFLICT_COPY;
		const bool fromConflict = conflictCopy || stage_ == DialogStage::SAVE_CONFLICT_COPY_AND_RELOAD;
		const bool reload = stage_ == DialogStage::SAVE_COPY_AND_RELOAD ||
		                    stage_ == DialogStage::SAVE_CONFLICT_COPY_AND_RELOAD;
		const auto result = fileDialog_.Draw(ui_);

		if (result == FileDialogResult::CANCELLED) {
			if (fromConflict) {
				errorOpenRequested_ = true;
				stage_ = DialogStage::ERROR;
			} else if (reload) {
				stage_ = DialogStage::IDLE;
				StartAction(EditorAction::RELOAD_SCENE);
			} else {
				stage_ = DialogStage::IDLE;
			}
		} else if (result == FileDialogResult::ACCEPTED) {
			if (document_.SaveCopy(fileDialog_.GetSelectedPath(),
			                       !fromConflict && !reload && !recoverySourceFile_)) {
				const auto &copy = fileDialog_.GetSelectedPath();
				recentScenes_.Add(copy);
				SaveEditorSession(false, copy);
				assets_.Refresh();
				stage_ = DialogStage::IDLE;
				if (reload) {
					ReloadScene(copy);
				} else if (conflictCopy) {
					errorActionMessage_ = "Saved a copy to " + PathUtils::ToUtf8(copy);
					errorOpenRequested_ = true;
					stage_ = DialogStage::ERROR;
				}
			} else {
				fileDialog_.RetrySelection(document_.GetError());
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
				suspendedRecyclePaths_.clear();
				assetSources_ = std::move(sources);
				assetsPanel_.Clear();
				assetsPanel_.RequestOpenDirectory({});
				stage_ = DialogStage::IDLE;
			} catch (const std::exception &error) {
				Logger::Error(
				    std::format("EditorScene::AssetsFolder: {}", error.what()));
				fileDialog_.RetrySelection("The current Assets folder was kept: " +
				                           std::string(error.what()));
			}
		}

		return;
	}

	case DialogStage::SELECT_ASSET_MOVE_DESTINATION: {
		const auto result = fileDialog_.Draw(ui_);
		if (result == FileDialogResult::CANCELLED) {
			errorOpenRequested_ = true;
			stage_ = DialogStage::ERROR;
		} else if (result == FileDialogResult::ACCEPTED) {
			const auto &directory = fileDialog_.GetSelectedPath();
			AssetUsage::Moves moves;
			for (const auto &[source, destination] : failedAssetMoves_) {
				if (!PathUtils::IsSameFile(source.parent_path(), directory)) {
					moves.emplace_back(source, directory / destination.filename());
				}
			}
			if (moves.empty()) {
				fileDialog_.RetrySelection("Choose a folder other than the source folder.");
				return;
			}

			std::string error;
			if (!RelocateAssets(moves, error)) {
				fileDialog_.RetrySelection(std::move(error));
				return;
			}

			if (failedAssetMoveWasCutPaste_) {
				assetsPanel_.FinishPaste();
			}
			failedAssetMoves_.clear();
			stage_ = DialogStage::IDLE;
		}
		return;
	}

	case DialogStage::SELECT_ASSET_COPY_DESTINATION: {
		const auto result = fileDialog_.Draw(ui_);
		if (result == FileDialogResult::CANCELLED) {
			errorOpenRequested_ = true;
			stage_ = DialogStage::ERROR;
		} else if (result == FileDialogResult::ACCEPTED) {
			std::string error;
			if (!CopyAssets(failedAssetCopy_.sources, fileDialog_.GetSelectedPath(), error)) {
				fileDialog_.RetrySelection(std::move(error));
				return;
			}
			failedAssetCopy_ = {};
			stage_ = DialogStage::IDLE;
		}
		return;
	}

	case DialogStage::EDIT_ANIMATION_CLIP: {
		const auto result = animationClipEditor_.Draw(ui_);
		if (result == AnimationClipEditor::Result::SAVED) {
			stage_ = DialogStage::IDLE;
			assets_.Refresh();
			assetsPanel_.RequestReveal(animationClipEditor_.GetSavedPath());
		} else if (result == AnimationClipEditor::Result::CANCELLED) {
			stage_ = DialogStage::IDLE;
		}
		break;
	}

	case DialogStage::EDIT_SCRIPT:
	case DialogStage::EDIT_SCENE_CHECK_SCRIPT:
	case DialogStage::EDIT_PLAY_SCRIPT:
	case DialogStage::EDIT_EXPORT_SCRIPT: {
		const bool fromExport = stage_ == DialogStage::EDIT_EXPORT_SCRIPT;
		const bool fromSceneCheck = stage_ == DialogStage::EDIT_SCENE_CHECK_SCRIPT;
		const auto *playScene = stage_ == DialogStage::EDIT_PLAY_SCRIPT ? &scriptReplayPath_ : nullptr;
		const auto result = scriptEditorDialog_.Draw(ui_, playScene);
		const bool playAfterSave = result == ScriptEditorDialog::Result::SAVED_AND_PLAY;
		const bool saved = result == ScriptEditorDialog::Result::SAVED || playAfterSave;
		if (saved) {
			sceneCheckPanel_.RequestRecheckIfOpen();
		}

		if (fromExport && result != ScriptEditorDialog::Result::NONE) {
			std::string error;
			if (result == ScriptEditorDialog::Result::SAVED) {
				if (!assets_.Refresh())
					error = assets_.GetError();
				CheckGameExport(true);
			}
			exportDialog_.Reopen(std::move(error));
			stage_ = DialogStage::EXPORT_GAME;
			break;
		}

		if (saved) {
			stage_ = DialogStage::IDLE;
			if (fromSceneCheck) {
				sceneCheckPanel_.Show();
			}
			const auto &savedScript = scriptEditorDialog_.GetSavedPath();
			if (!assets_.Refresh() && playAfterSave) {
				Logger::Warning("The script was saved, but Assets could not refresh: " +
				                    assets_.GetError(),
				                savedScript);
				consolePanel_.Show();
			}
			assetsPanel_.RequestReveal(savedScript);
			if (playAfterSave) {
				StartPlay(scriptReplayPath_, scriptReplayCameraOverride_, true);
			}
		} else if (result == ScriptEditorDialog::Result::CANCELLED)
			stage_ = DialogStage::IDLE;
		break;
	}

	case DialogStage::CREATE_PREFAB: {
		const auto result = prefabCreateDialog_.Draw(ui_);

		if (result == PrefabCreateResult::ACCEPTED) {
			stage_ = DialogStage::IDLE;

			assets_.Refresh();
			assetsPanel_.RequestReveal(prefabCreateDialog_.GetCreatedPath());
		} else if (result == PrefabCreateResult::CANCELLED) {
			stage_ = DialogStage::IDLE;
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

			SaveEditorSession();
			document_.SetUntitled();
			if (lastPlayScenePath_.empty()) {
				lastPlayCameraOverride_ = 0;
				lastPlayFollowedEditingScene_ = false;
			}
			if (playFailureReplay_ && playFailureReplay_->path.empty()) {
				playFailureReplay_.reset();
			}
			exportDialog_.ForgetUntitledDraft();
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

			assets_.Refresh();
			assetsPanel_.RequestReveal(modelImportDialog_.GetImportedPath());
		} else if (result == ModelImportResult::CANCELLED) {
			stage_ = DialogStage::IDLE;
		}

		return;
	}

	case DialogStage::IMPORT_TEXTURE: {
		const auto result = textureImportDialog_.Draw(ui_);

		if (result == TextureImportResult::ACCEPTED) {
			stage_ = DialogStage::IDLE;

			assets_.Refresh();
			assetsPanel_.RequestReveal(textureImportDialog_.GetImportedPath());
		} else if (result == TextureImportResult::CANCELLED) {
			stage_ = DialogStage::IDLE;
		}

		return;
	}

	case DialogStage::IMPORT_AUDIO: {
		const auto result = audioImportDialog_.Draw(ui_);

		if (result == AudioImportResult::ACCEPTED) {
			stage_ = DialogStage::IDLE;

			assets_.Refresh();
			assetsPanel_.RequestReveal(audioImportDialog_.GetImportedPath());
		} else if (result == AudioImportResult::CANCELLED) {
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
				Collider bounds;
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

	case DialogStage::EXPORT_RESULT: {
		const auto result = exportResultDialog_.Draw(ui_);
		if (result != GameExportResultAction::NONE) {
			stage_ = DialogStage::IDLE;
			if (result == GameExportResultAction::NEW_EXPORT) {
				ShowGameExport(exportResultDialog_.GetDirectory());
			}
		}

		return;
	}

	case DialogStage::SELECT_GAME_EXPORT: {
		const auto result = fileDialog_.Draw(ui_);
		if (result == FileDialogResult::CANCELLED) {
			pendingGameExportReplacement_.clear();
			stage_ = DialogStage::IDLE;
		} else if (result == FileDialogResult::ACCEPTED &&
		           !OpenGameExport(fileDialog_.GetSelectedPath())) {
			fileDialog_.RetrySelection(std::format(
			                               "{}\n{}",
			                               PathUtils::ToUtf8(fileDialog_.GetSelectedPath()),
			                               FileDialog::GameExportOpenProblem(fileDialog_.GetSelectedPath())),
			                           pendingGameExportReplacement_);
		}
		return;
	}

	case DialogStage::SELECT_STARTUP_SCENE:
	case DialogStage::SELECT_FAILED_STARTUP_SCENE:
	case DialogStage::SELECT_RECYCLE_STARTUP_SCENE:
	case DialogStage::SELECT_EXPORT_STARTUP_SCENE: {
		const bool fromExport = stage_ == DialogStage::SELECT_EXPORT_STARTUP_SCENE;
		const bool fromRecycle = stage_ == DialogStage::SELECT_RECYCLE_STARTUP_SCENE;
		const bool fromFailure = stage_ == DialogStage::SELECT_FAILED_STARTUP_SCENE;
		const auto result = fileDialog_.Draw(ui_);
		if (const auto search = fileDialog_.TakeProjectSearchChange()) {
			preferences_.searchProjectScenes = *search;
			SaveEditorSession(true);
		}
		if (result == FileDialogResult::NONE) {
			return;
		}
		if (result == FileDialogResult::ACCEPTED) {
			std::string error;
			if (!SetStartupScene(fileDialog_.GetSelectedPath(), error)) {
				fileDialog_.RetrySelection(std::move(error));
				return;
			}
			if (fromExport) {
				const auto startup = project_.GetStartupScene();
				exportDialog_.UpdateStartupScene(
				    startup, UsesEditingSceneForStartup(startup) && document_.IsModified());
				CheckGameExport();
			}
		}
		stage_ = DialogStage::IDLE;
		if (fromExport) {
			exportDialog_.Reopen();
			stage_ = DialogStage::EXPORT_GAME;
		} else if (fromRecycle) {
			CheckAssetReferences(true);
			stage_ = DialogStage::RECYCLE_ASSET;
		} else if (fromFailure) {
			if (result == FileDialogResult::CANCELLED) {
				errorOpenRequested_ = true;
				stage_ = DialogStage::ERROR;
			} else if (errorLoadingPlayScene_) {
				StartPlay(project_.GetStartupScene(), errorReplayCameraOverride_);
			} else {
				pendingOpenPath_ = project_.GetStartupScene();
				StartAction(EditorAction::OPEN_PATH);
			}
		}
		return;
	}

	case DialogStage::SCENE_SETTINGS: {
		constexpr const char *kTitle = "Scene Settings";

		if (sceneSettingsOpenRequested_) {
			ui_.OpenPopup(kTitle);
			sceneSettingsOpenRequested_ = false;
		}

		if (ui_.BeginPopupModal(kTitle)) {
			ImGui::Checkbox("Override Project Ambient Light", &sceneAmbientOverride_);

			ImGui::BeginDisabled(!sceneAmbientOverride_);
			float ambient = sceneAmbientOverride_ ? sceneAmbientDraft_
			                                     : exportConfig_.lighting.ambient;
			if (ImGui::SliderFloat("Ambient Light", &ambient, 0.0f, 1.0f,
			                       "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
				sceneAmbientDraft_ = ambient;
			}
			ImGui::EndDisabled();

			ImGui::Separator();
			ImGui::Checkbox("Distance Fog", &sceneFogDraft_.enabled);
			ImGui::BeginDisabled(!sceneFogDraft_.enabled);
			ImGui::ColorEdit3("Fog Color", &sceneFogDraft_.color.x);
			ImGui::InputFloat("Fog Start", &sceneFogDraft_.start);
			ImGui::InputFloat("Fog End", &sceneFogDraft_.end);
			ImGui::SliderFloat("Fog Strength", &sceneFogDraft_.strength, 0.0f,
			                   1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::EndDisabled();

			const bool validFog = sceneFogDraft_.IsValid();
			if (!validFog) {
				ImGui::TextUnformatted("Fog End must be greater than Fog Start (0 or above).");
			}
			bool applied = false;
			ImGui::BeginDisabled(!validFog);
			if (ui_.Button("Apply")) {
				std::optional<LightingSettings> lighting;
				if (sceneAmbientOverride_) {
					lighting = document_.GetScene().GetLighting().value_or(
					    exportConfig_.lighting);
					lighting->ambient = sceneAmbientDraft_;
				}
				applied = document_.GetHistory().SetSceneLighting(lighting, sceneFogDraft_);
			}
			ImGui::EndDisabled();

			ui_.SameLine();
			if (ui_.Button("Cancel") || applied) {
				ui_.CloseCurrentPopup();
				stage_ = DialogStage::IDLE;
			}

			ui_.EndPopup();
		}

		return;
	}
	case DialogStage::GAME_SETTINGS:
	case DialogStage::EDIT_PLAY_INPUT:
	case DialogStage::EDIT_CONSOLE_PLAY_INPUT:
	case DialogStage::EDIT_EXPORT_INPUT: {
		const bool fromExport = stage_ == DialogStage::EDIT_EXPORT_INPUT;
		const bool fromPlayError = stage_ == DialogStage::EDIT_PLAY_INPUT;
		const bool fromConsole = stage_ == DialogStage::EDIT_CONSOLE_PLAY_INPUT;
		const bool fromPlay = fromPlayError || fromConsole;
		const auto result = gameSettingsDialog_.Draw(ui_, fromPlay);

		if (result == GameSettingsResult::ACCEPTED) {
			const auto &settings = gameSettingsDialog_.GetConfig();
			std::string error;

			if (!project_.GetRoot().empty() &&
			    !project_.Save(settings, project_.GetStartupScene(), error)) {
				gameSettingsDialog_.Reopen("Could not save Game Settings: " + error,
				                           project_.GetRoot() / L"project.json");
				return;
			}

			exportConfig_ = settings;
			sceneCheckPanel_.RequestRecheckIfOpen();
		}

		if (result != GameSettingsResult::NONE) {
			if (!gameSettingsDialog_.GetLastSavedPath().empty()) {
				assets_.Refresh();
			}

			if (fromExport) {
				if (result == GameSettingsResult::ACCEPTED) {
					CheckGameExport(true);
				}
				exportDialog_.Reopen();
				stage_ = DialogStage::EXPORT_GAME;
			} else if (fromConsole) {
				stage_ = DialogStage::IDLE;
				if (result == GameSettingsResult::ACCEPTED && playFailureReplay_) {
					const auto retry = *playFailureReplay_;
					StartPlay(retry.path, retry.camera, true);
				}
			} else if (fromPlayError) {
				if (result == GameSettingsResult::ACCEPTED && errorReplayPath_) {
					const auto path = *errorReplayPath_;
					stage_ = DialogStage::IDLE;
					StartPlay(path, errorReplayCameraOverride_, true);
				} else {
					errorOpenRequested_ = true;
					stage_ = DialogStage::ERROR;
				}
			} else {
				stage_ = DialogStage::IDLE;
			}
		}

		return;
	}
	case DialogStage::EXPORT_GAME: {
		const bool canSaveAndCheck =
		    document_.IsModified() && !document_.GetFilePath().empty() &&
		    !UsesEditingSceneForStartup(project_.GetStartupScene());
		const auto result = exportDialog_.Draw(ui_, canSaveAndCheck);
		const auto &root = project_.GetRoot();
		if (result != GameExportResult::NONE) {
			bool changed = false;
			if (auto draft = exportDialog_.GetDraft()) {
				preferences_.gameExportDrafts.insert_or_assign(root, std::move(*draft));
				changed = true;
			} else if (result == GameExportResult::CANCELLED) {
				changed = preferences_.gameExportDrafts.erase(root) != 0;
			}
			if (changed && canSavePreferences_ && !preferences_.Save()) {
				consolePanel_.Show();
			}
		}
		if (result == GameExportResult::CANCELLED) {
			stage_ = DialogStage::IDLE;
			return;
		}
		if (result == GameExportResult::SAVE_AND_CHECK) {
			pendingAction_ = EditorAction::SAVE_AND_CHECK_EXPORT;
			StartSave(false);
			return;
		}
		if (result == GameExportResult::CHOOSE_STARTUP_SCENE) {
			BrowseStartupScene();
			stage_ = DialogStage::SELECT_EXPORT_STARTUP_SCENE;
			return;
		}

		if (result == GameExportResult::OPEN_ISSUE ||
		    result == GameExportResult::OPEN_TRANSITION_SOURCE) {
			const auto &issue = exportDialog_.GetSelectedIssue();
			const bool source = result == GameExportResult::OPEN_TRANSITION_SOURCE;
			const auto &scenePath = source ? issue.transitionScenePath : issue.scenePath;
			const auto object = source ? issue.transitionObjectId : issue.objectId;
			const auto &current = document_.GetFilePath();
			if (scenePath.empty() ||
			    (!current.empty() && PathUtils::IsSameFile(current, scenePath))) {
				stage_ = DialogStage::IDLE;
				RevealGameExportObject(object);
			} else if (playMode_.IsPlaying()) {
				exportDialog_.Reopen("Stop Play before opening the referenced scene.");
			} else {
				stage_ = DialogStage::IDLE;
				pendingOpenPath_ = scenePath;
				StartAction(EditorAction::OPEN_EXPORT_ISSUE, object);
			}
			return;
		}
		if (result == GameExportResult::EDIT_ISSUE_SCRIPT) {
			const auto &issue = exportDialog_.GetSelectedIssue();
			std::string error;
			if (scriptEditorDialog_.ShowEdit(issue.scriptPath, error, issue.message,
			                                 issue.scriptDiagnosticPath)) {
				stage_ = DialogStage::EDIT_EXPORT_SCRIPT;
			} else {
				exportDialog_.Reopen("Could not open the script: " + error);
			}
			return;
		}
		if (result == GameExportResult::EDIT_ISSUE_INPUT) {
			gameSettingsDialog_.Show(exportConfig_, project_.GetRoot(), {},
			                         exportDialog_.GetSelectedIssue().inputAction);
			stage_ = DialogStage::EDIT_EXPORT_INPUT;
			return;
		}
		if (result == GameExportResult::REVEAL_ISSUE_ASSET) {
			try {
				const auto &path = exportDialog_.GetSelectedIssue().assetPath;
				if (assetsPanel_.RevealIndexedAsset(path, assets_) ||
				    (assets_.Refresh() && assetsPanel_.RevealIndexedAsset(path, assets_))) {
					stage_ = DialogStage::IDLE;
				} else {
					exportDialog_.Reopen("This file is unavailable in Assets. Use Show in Explorer to locate it.");
				}
			} catch (const std::exception &error) {
				exportDialog_.Reopen(error.what());
			}
			return;
		}
		if (result == GameExportResult::ACCEPTED) {
			stage_ = DialogStage::IDLE;
			StartAction(EditorAction::WRITE_GAME_EXPORT);
			return;
		}
		if (result == GameExportResult::CHECK) {
			CheckGameExport(true);
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

			const auto startup = project_.GetStartupScene();
			const auto &failedStartupCandidate =
			    errorLoadingPlayScene_ ? errorScenePath_ : failedOpenScenePath_;
			if (!startup.empty() && !failedStartupCandidate.empty() &&
			    PathUtils::IsSameFile(startup, failedStartupCandidate) &&
			    !playMode_.IsPlaying()) {
				if (ui_.Button(errorLoadingPlayScene_ ? "Choose Startup Scene and Retry Play..."
				                                     : "Choose Startup Scene and Open...")) {
					ui_.CloseCurrentPopup();
					ui_.EndPopup();
					BrowseStartupScene(true);
					stage_ = DialogStage::SELECT_FAILED_STARTUP_SCENE;
					return;
				}
			}

			if (!failedOpenScenePath_.empty()) {
				if (ui_.Button("Show Scene Location")) {
					ShellUtils::RevealLocationInExplorer(failedOpenScenePath_,
					                                     errorActionMessage_);
				}
				ui_.SameLine();
				if (!reloadBackupPath_.empty()) {
					if (ui_.Button("Open Saved Copy")) {
						pendingOpenPath_ = reloadBackupPath_;
						ui_.CloseCurrentPopup();
						ui_.EndPopup();
						stage_ = DialogStage::IDLE;
						StartAction(EditorAction::OPEN_PATH);
						return;
					}
					ImGui::SetItemTooltip("%s", PathUtils::ToUtf8(reloadBackupPath_).c_str());
					ui_.SameLine();
				}
				if (!failedRestoredScene_.empty() && failedOpenScenePath_ == failedRestoredScene_) {
					if (ui_.Button("Locate Last Edited Scene...")) {
						ui_.CloseCurrentPopup();
						ui_.EndPopup();
						stage_ = DialogStage::IDLE;
						StartAction(EditorAction::BROWSE_LAST_EDITED_SCENE);
						return;
					}
					ui_.SameLine();
				}
				if (ui_.Button(sceneReloadFailed_ ? "Retry Reload" : "Retry Open Scene")) {
					auto backup = std::exchange(reloadBackupPath_, {});
					auto path = std::exchange(failedOpenScenePath_, {});
					const auto object =
					    std::exchange(failedOpenSceneObject_, SceneObjectId{0});
					ui_.CloseCurrentPopup();
					ui_.EndPopup();
					stage_ = DialogStage::IDLE;
					if (sceneReloadFailed_) {
						ReloadScene(backup);
					} else {
						pendingOpenPath_ = std::move(path);
						StartAction(playMode_.IsPlaying() ? EditorAction::OPEN_PLAY_SOURCE
						                                  : EditorAction::OPEN_PATH,
						            object);
					}
					return;
				}
				ui_.SameLine();
			}

			if (!errorScenePath_.empty()) {
				if (ui_.Button("Show Play Scene Location")) {
					ShellUtils::RevealLocationInExplorer(errorScenePath_,
					                                     errorActionMessage_);
				}
				ImGui::SetItemTooltip(
				    "Show the scene file or its nearest existing folder in Explorer.");
				std::error_code status;
				if (std::filesystem::is_regular_file(errorScenePath_, status)) {
					ui_.SameLine();
					const bool playing = playMode_.IsPlaying();
					if (ui_.Button(playing ? "Stop and Open Play Scene" : "Open Play Scene")) {
						pendingOpenPath_ = std::move(errorScenePath_);
						const auto object =
						    std::exchange(errorObjectId_, SceneObjectId{0});
						ui_.CloseCurrentPopup();
						ui_.EndPopup();
						if (playing) {
							playMode_.Stop();
							ClearPlaySelection();
						}
						stage_ = DialogStage::IDLE;
						StartAction(EditorAction::OPEN_PATH, object);
						return;
					}
				}
				ui_.SameLine();
			}

			if (!failedAssetMoves_.empty()) {
				if (ui_.Button("Show Destination Folder")) {
					ShellUtils::RevealLocationInExplorer(
					    failedAssetMoves_.front().second.parent_path(), errorActionMessage_);
				}
				ui_.SameLine();
				if (ui_.Button("Choose Another Destination...")) {
					auto directory = FindExistingDirectory(
					    failedAssetMoves_.front().second.parent_path());
					if (directory.empty()) {
						directory = assets_.GetRootDirectory();
					}
					fileDialog_.Show(FileDialogMode::SELECT_DIRECTORY, directory,
					                 FileDialogFilter::SCENE,
					                 "Choose Move Destination###FileDialog");
					ui_.CloseCurrentPopup();
					ui_.EndPopup();
					stage_ = DialogStage::SELECT_ASSET_MOVE_DESTINATION;
					return;
				}
				ui_.SameLine();
				const bool retry = ui_.Button("Retry Move");
				ui_.SameLine();
				const bool useAvailableNames = ui_.Button("Retry with Available Names");
				ImGui::SetItemTooltip("Add a suffix to conflicting names and retry the move.");
				if (retry || useAvailableNames) {
					std::string error;
					const auto moves = useAvailableNames
					                       ? AssetUsage::FindAvailableMoveDestinations(failedAssetMoves_, error)
					                       : failedAssetMoves_;
					if (!moves.empty() && RelocateAssets(moves, error)) {
						if (failedAssetMoveWasCutPaste_) {
							assetsPanel_.FinishPaste();
						}
						failedAssetMoves_.clear();
						ui_.CloseCurrentPopup();
						ui_.EndPopup();
						stage_ = DialogStage::IDLE;
						return;
					}
					errorMessage_ = std::move(error);
					errorActionMessage_.clear();
				}
				ui_.SameLine();
			}

			if (!failedAssetCopy_.sources.empty()) {
				if (ui_.Button("Choose Copy Destination...")) {
					const auto &preferred = failedAssetCopy_.directory;
					const auto initial = FindExistingDirectory(
					    preferred.empty() ? failedAssetCopy_.sources.front().parent_path()
					                      : preferred);
					fileDialog_.Show(FileDialogMode::SELECT_DIRECTORY,
					                 initial.empty() ? assets_.GetRootDirectory() : initial,
					                 FileDialogFilter::SCENE,
					                 "Choose Copy Destination###FileDialog");
					ui_.CloseCurrentPopup();
					ui_.EndPopup();
					stage_ = DialogStage::SELECT_ASSET_COPY_DESTINATION;
					return;
				}
				ui_.SameLine();
				if (ui_.Button(failedAssetCopy_.directory.empty() ? "Retry Duplicate" : "Retry Copy")) {
					std::string error;
					if (CopyAssets(failedAssetCopy_.sources, failedAssetCopy_.directory, error)) {
						failedAssetCopy_ = {};
						ui_.CloseCurrentPopup();
						ui_.EndPopup();
						stage_ = DialogStage::IDLE;
						return;
					}
					errorMessage_ = std::move(error);
				}
				ui_.SameLine();
			}

			if (sceneSaveFailed_) {
				if (ui_.Button("Show Save Location")) {
					ShellUtils::RevealLocationInExplorer(document_.GetFilePath(),
					                                     errorActionMessage_);
				}
				ui_.SameLine();
				if (ui_.Button("Retry Save")) {
					ui_.CloseCurrentPopup();
					ui_.EndPopup();
					stage_ = DialogStage::IDLE;
					StartSave(false);
					return;
				}
				if (sceneSaveConflict_) {
					ImGui::SetItemTooltip(
					    "Retry after restoring the file to the version opened in the editor.");
				}
				ui_.SameLine();
				if (ui_.Button(pendingAction_ == EditorAction::NONE
				                   ? "Save As..."
				                   : "Save As and Continue...")) {
					try {
						fileDialog_.Show(FileDialogMode::SAVE, GetSceneCopyPath());
						fileDialog_.RequireNewFile();
						ui_.CloseCurrentPopup();
						ui_.EndPopup();
						stage_ = DialogStage::SAVE_FILE_AFTER_ERROR;
						return;
					} catch (const std::exception &error) {
						errorActionMessage_ = error.what();
					}
				}
				ui_.SameLine();
			}

			if (sceneSaveConflict_) {
				if (ui_.Button(pendingAction_ == EditorAction::NONE
				                   ? "Save Copy and Reload..."
				                   : "Save Copy, Reload and Continue...")) {
					ui_.CloseCurrentPopup();
					ui_.EndPopup();
					StartSaveCopy(true);
					if (stage_ == DialogStage::SAVE_COPY_AND_RELOAD) {
						stage_ = DialogStage::SAVE_CONFLICT_COPY_AND_RELOAD;
					}
					return;
				}
				ImGui::SetItemTooltip(
				    "Save your edits to a separate scene, then reload the original.");
				ui_.SameLine();
				EditorAction action = EditorAction::NONE;
				if (ui_.Button("Save Copy...")) {
					try {
						fileDialog_.Show(FileDialogMode::SAVE_COPY, GetSceneCopyPath());
						fileDialog_.RequireNewFile();
						ui_.CloseCurrentPopup();
						ui_.EndPopup();
						stage_ = DialogStage::SAVE_CONFLICT_COPY;
						return;
					} catch (const std::exception &error) {
						errorActionMessage_ = error.what();
					}
				}
				ui_.SameLine();
				if (ui_.Button("Reload Scene...")) {
					action = EditorAction::RELOAD_SCENE;
				}
				ui_.SameLine();
				if (action != EditorAction::NONE) {
					ui_.CloseCurrentPopup();
					ui_.EndPopup();
					stage_ = DialogStage::IDLE;
					StartAction(action);
					return;
				}
			}

			if (!errorActionMessage_.empty()) {
				ImGui::TextWrapped("%s", errorActionMessage_.c_str());
			}

			if (!errorInputAction_.empty() && errorReplayPath_ && !playMode_.IsPlaying()) {
				if (ui_.Button("Edit Input Binding")) {
					gameSettingsDialog_.Show(exportConfig_, project_.GetRoot(), {},
					                         errorInputAction_);
					ui_.CloseCurrentPopup();
					ui_.EndPopup();
					stage_ = DialogStage::EDIT_PLAY_INPUT;
					return;
				}
				ui_.SameLine();
			}

			if (!errorScriptPath_.empty()) {
				if (ui_.Button(playMode_.IsPlaying() ? "Stop and Edit Script" : "Edit Script")) {
					std::string error;
					if (scriptEditorDialog_.ShowEdit(errorScriptPath_, error,
					                                 errorMessage_)) {
						ui_.CloseCurrentPopup();
						if (errorReplayPath_) {
							if (playMode_.IsPlaying()) {
								playMode_.Stop();
								ClearPlaySelection();
							}
							scriptReplayPath_ = *errorReplayPath_;
							scriptReplayCameraOverride_ = errorReplayCameraOverride_;
							stage_ = DialogStage::EDIT_PLAY_SCRIPT;
						} else {
							stage_ = DialogStage::EDIT_SCRIPT;
						}
						ui_.EndPopup();
						return;
					}
					errorActionMessage_ = "Could not open the script: " + error;
				}
				ui_.SameLine();
			}

			if (errorScenePath_.empty() &&
			    document_.GetScene().Contains(errorObjectId_)) {
				const bool playing = playMode_.IsPlaying();
				if (ui_.Button(playing ? "Stop and Show Object" : "Show Object")) {
					try {
						if (playing) {
							playMode_.Stop();
							ClearPlaySelection();
						}
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

			if (!errorReloadAssetPath_.empty()) {
				if (ui_.Button("Show in Explorer")) {
					ShellUtils::RevealLocationInExplorer(errorReloadAssetPath_,
					                                     errorActionMessage_);
				}
				ui_.SameLine();
				if (ui_.Button("Retry Reload")) {
					auto path = std::exchange(errorReloadAssetPath_, std::filesystem::path{});
					ui_.CloseCurrentPopup();
					ui_.EndPopup();
					stage_ = DialogStage::IDLE;
					ReloadAsset(path, errorReloadIsTexture_);
					return;
				}
				ui_.SameLine();
			}
			if (errorRestartCurrentScene_ && playMode_.IsPlaying()) {
				if (ui_.Button("Retry Restart")) {
					const bool currentScene = *errorRestartCurrentScene_;
					ui_.CloseCurrentPopup();
					ui_.EndPopup();
					CancelPendingAction();
					RestartPlay(currentScene);
					return;
				}
				ui_.SameLine();
			}
			if (errorReplayPath_ && !playMode_.IsPlaying()) {
				if (ui_.Button("Play Scene Again")) {
					auto path = *errorReplayPath_;
					const auto camera = errorReplayCameraOverride_;
					ui_.CloseCurrentPopup();
					ui_.EndPopup();
					CancelPendingAction();
					StartPlay(path, camera, true);
					return;
				}
				ImGui::SetItemTooltip("Start Play in %s.",
				                      errorReplayPath_->empty()
				                          ? "the editing scene"
				                          : PathUtils::ToUtf8(*errorReplayPath_).c_str());
				ui_.SameLine();
			}
			const bool canShowPlayLog = errorPlayLogId_ != 0;
			const bool showPlayError = canShowPlayLog && ui_.Button("Show in Console");
			if (canShowPlayLog) {
				ui_.SameLine();
			}
			if (ui_.Button("OK") || showPlayError) {
				if (showPlayError) {
					consolePanel_.ShowPlayError(errorPlayLogId_);
				}
				ui_.CloseCurrentPopup();
				errorObjectId_ = 0;
				failedAssetMoves_.clear();
				failedAssetCopy_ = {};
				CancelPendingAction();
			}

			ui_.EndPopup();
		}

		return;
	}
	}
}

bool EditorScene::UsesEditingSceneForStartup(
    const std::filesystem::path &startup) const {
	const auto &current = document_.GetFilePath();
	return startup.empty() ||
	       (!current.empty() && PathUtils::IsSameFile(startup, current));
}

const Scene *EditorScene::GetStartupSceneForRun(SceneDocument &startup,
                                                bool reportError) {
	const auto path = project_.GetStartupScene();
	if (UsesEditingSceneForStartup(path)) {
		return &document_.GetScene();
	}

	if (!startup.Open(path, &exportConfig_.lighting)) {
		if (reportError) {
			ShowError("Could not load the project startup scene: " +
			          startup.GetError());
		}
		return nullptr;
	}

	return &startup.GetScene();
}

void EditorScene::StartPlay(const std::filesystem::path &path,
                            SceneObjectId cameraOverride, bool allowCameraFallback) {
	consolePanel_.MarkPlayStart();
	playFailureReplay_.reset();
	audioPreviewPanel_.Stop();
	frameRequest_ = FrameRequest::NONE;
	frameObject_ = 0;
	FinishEdit();
	sceneViewPanel_.CancelSelection();

	SceneDocument startup;
	const bool usesEditingScene = UsesEditingSceneForStartup(path);
	if (!usesEditingScene && !startup.Open(path, &exportConfig_.lighting)) {
		auto error = "Could not load the play scene: " + PathUtils::ToUtf8(path) +
		             "\n" + startup.GetError();
		ShowError(std::move(error));
		errorScenePath_ = path;
		errorLoadingPlayScene_ = true;
		errorReplayPath_ = path;
		errorReplayCameraOverride_ = cameraOverride;
		LogPlayFailure();
		return;
	}

	// 試遊中は定期記録が止まるため、実行前に未保存の編集を記録する。
	RecordRecovery();

	const auto *scene = usesEditingScene ? &document_.GetScene() : &startup.GetScene();
	if (allowCameraFallback && cameraOverride != 0) {
		const auto *object = scene->FindObject(cameraOverride);
		if (!object || !object->camera || !SceneActivity::IsActive(*scene, cameraOverride)) {
			Logger::Warning(
			    "The previous Play camera is unavailable. Using normal camera selection.",
			    usesEditingScene ? document_.GetFilePath() : startup.GetFilePath());
			consolePanel_.Show();
			cameraOverride = 0;
		}
	}
	const auto fallback = usesEditingScene ? cameraController_.GetPose()
	                                       : exportConfig_.fallbackCamera;
	std::string error;
	SceneObjectId invalidObject = 0;
	std::string invalidInputAction;
	if (!playMode_.Start(*scene, error, fallback, exportConfig_, &invalidObject,
	                     cameraOverride, &invalidInputAction)) {
		auto scriptPath = SceneScripts::FindFailedScript(*scene, invalidObject, error);
		ShowError(std::move(error), usesEditingScene ? invalidObject : 0,
		          std::move(scriptPath));
		errorInputAction_ = std::move(invalidInputAction);
		errorReplayPath_ = usesEditingScene ? document_.GetFilePath()
		                                   : startup.GetFilePath();
		errorReplayCameraOverride_ = cameraOverride;
		if (!usesEditingScene) {
			errorScenePath_ = startup.GetFilePath();
			errorMessage_ += "\nPlay scene: " + PathUtils::ToUtf8(errorScenePath_);
			// 開いている編集シーンとIDが重複しても、起動シーンを開いてから選択する。
			errorObjectId_ = scene->Contains(invalidObject) ? invalidObject : 0;
		}
		LogPlayFailure();
		return;
	}

	playUsesEditingScene_ = usesEditingScene;
	playInitialScenePath_ =
	    usesEditingScene ? document_.GetFilePath() : startup.GetFilePath();
	lastPlayScenePath_ = playInitialScenePath_;
	lastPlayFollowedEditingScene_ = usesEditingScene;
	lastPlayCameraOverride_ = cameraOverride;
	playInitialCameraOverride_ = cameraOverride;
}

void EditorScene::RestartPlay(bool currentScene) {
	std::string error;
	consolePanel_.MarkPlayStart();
	playFailureReplay_.reset();

	std::filesystem::path failedScript;
	SceneObjectId invalidObject = 0;
	const auto *loaded = playMode_.GetSession().GetLoadedScenePath();
	const bool retryLoaded = currentScene && loaded;
	const auto retryPath = retryLoaded ? *loaded : playInitialScenePath_;
	if (!playMode_.Restart(error, currentScene, &failedScript, &invalidObject)) {
		const auto &current = document_.GetFilePath();
		const bool isEditingScene = retryPath.empty() ||
		    (!current.empty() && PathUtils::IsSameFile(current, retryPath));
		if (!retryPath.empty()) {
			error += "\nPlay scene: " + PathUtils::ToUtf8(retryPath);
		}
		ShowError(std::move(error), isEditingScene ? invalidObject : 0,
		          std::move(failedScript));
		errorReplayPath_ = retryPath;
		errorReplayCameraOverride_ = retryLoaded ? 0 : playInitialCameraOverride_;
		if (!isEditingScene) {
			errorScenePath_ = retryPath;
			errorObjectId_ = invalidObject;
		}
		errorRestartCurrentScene_ = currentScene;
		LogPlayFailure();
	} else {
		SynchronizePlayScene();
	}
}

void EditorScene::ReportPlayUpdateFailure(SceneObjectId invalidObject,
                                          std::uint64_t previousRevision) {
	const auto &reason = playMode_.GetLastUpdateError();
	auto message = reason.empty()
	                   ? "Play stopped because simulation or Game rendering failed."
	                   : "Play stopped: " + reason;
	const auto &session = playMode_.GetSession();
	if (const auto failed = session.GetFailedSceneDestination()) {
		auto path = failed->path;
		auto scriptPath = failed->scriptPath;
		auto inputAction = failed->inputAction;
		const auto object = failed->objectId;
		const auto &current = document_.GetFilePath();
		const bool isEditingScene =
		    !current.empty() && PathUtils::IsSameFile(current, path);
		playMode_.Stop();
		ShowError(std::move(message), isEditingScene ? object : 0,
		          std::move(scriptPath));
		errorInputAction_ = std::move(inputAction);
		// 到着タグや共有状態を使う遷移を再現できるよう、最初のシーンから試遊する。
		errorReplayPath_ = playInitialScenePath_;
		errorReplayCameraOverride_ = playInitialCameraOverride_;
		if (!isEditingScene) {
			errorScenePath_ = std::move(path);
			errorObjectId_ = object;
		}
		LogPlayFailure();
		return;
	}

	const auto *loaded = session.GetLoadedScenePath();
	bool isEditingScene = playUsesEditingScene_ && !loaded;
	auto scenePath = loaded ? *loaded : playInitialScenePath_;
	// 遷移直後の描画・音声エラーでも、到着時の状態を開始シーンから再現する。
	const bool replayFromStart = !loaded || session.GetSceneRevision() != previousRevision;
	const auto replayPath = replayFromStart ? playInitialScenePath_ : scenePath;
	const auto replayCamera = replayFromStart ? playInitialCameraOverride_ : SceneObjectId{0};
	if (!scenePath.empty()) {
		message += "\nPlay scene: " + PathUtils::ToUtf8(scenePath);
		const auto &current = document_.GetFilePath();
		isEditingScene =
		    !current.empty() && PathUtils::IsSameFile(current, scenePath);
		if (isEditingScene) {
			scenePath.clear();
		}
	}
	const auto sourceObject =
	    session.HasSourceObject(invalidObject) ? invalidObject : 0;
	auto scriptPath =
	    SceneScripts::FindFailedScript(session.GetScene(), invalidObject, reason);
	playMode_.Stop();
	ShowError(std::move(message), isEditingScene ? sourceObject : 0,
	          std::move(scriptPath));
	errorReplayPath_ = replayPath;
	errorReplayCameraOverride_ = replayCamera;
	errorScenePath_ = std::move(scenePath);
	if (!errorScenePath_.empty()) {
		errorObjectId_ = sourceObject;
	}
	LogPlayFailure();
}

void EditorScene::LogPlayFailure() {
	const auto &source = errorScriptPath_.empty() ? errorScenePath_ : errorScriptPath_;
	const auto logId = Logger::PlayFailure(errorMessage_, source);
	errorPlayLogId_ = logId;
	if (errorReplayPath_) {
		const auto &editingPath = document_.GetFilePath();
		const bool followsEditingScene = errorReplayPath_->empty() ||
		    (!editingPath.empty() && PathUtils::IsSameFile(*errorReplayPath_, editingPath));
		playFailureReplay_ = PlayFailureReplay{
		    logId, followsEditingScene ? editingPath : *errorReplayPath_,
		    errorReplayCameraOverride_, followsEditingScene, errorScriptPath_, errorInputAction_};
	}
}

void EditorScene::SwitchProject(const std::filesystem::path &folder,
                                bool create, bool reopenOnFailure) {
	const auto reportFailure = [&](std::string error) {
		if (stage_ == DialogStage::CREATE_PROJECT ||
		    stage_ == DialogStage::OPEN_PROJECT) {
			const auto &retryProject = missingRecentProject_.empty()
			                               ? folder : missingRecentProject_;
			if (retryProject != folder) {
				error = std::format("Could not open {}.\n{}", PathUtils::ToUtf8(folder), error);
			}
			projectDialog_.RetrySelection(std::move(error), retryProject);
		} else if (reopenOnFailure) {
			Logger::Error("Could not reopen project: " + error, folder / L"project.json");
			std::error_code status;
			if (std::filesystem::symlink_status(folder / L"project.json", status)
			        .type() == std::filesystem::file_type::not_found) {
				missingRecentProject_ = folder;
			}
			projectDialog_.Show(FileDialogMode::SELECT_DIRECTORY,
			                 FindExistingDirectory(folder), FileDialogFilter::PROJECT,
			                 "Open Project###FileDialog");
			projectDialog_.RetrySelection(std::move(error), folder);
			if (!missingRecentProject_.empty() &&
			    (preferences_.projectScenes.contains(missingRecentProject_) ||
			     !preferences_.LastProjectAssetFolder(missingRecentProject_).empty())) {
				projectDialog_.OfferPreviousProjectSession();
			}
			stage_ = DialogStage::OPEN_PROJECT;
		} else {
			ShowError(std::move(error));
		}
	};
	try {
		ProjectDocument candidate;
		std::string error;

		if (!(create ? candidate.Create(folder, error)
		             : candidate.Open(folder, error))) {
			reportFailure(std::move(error));
			return;
		}

		AssetDatabase assets(candidate.GetAssetsDirectory(),
		                     candidate.GetScenesDirectory());

		if (!assets.Refresh()) {
			reportFailure(assets.GetError());
			return;
		}

		AssetSources sources;
		sources.Select(assets.GetRootDirectory());
		sources.Register(candidate.GetScenesDirectory());
		auto config = candidate.GetGameConfig();
		const auto startup = candidate.GetStartupScene();
		std::filesystem::path unavailableLast;
		const auto previousProjectRoot = !create && stage_ == DialogStage::OPEN_PROJECT &&
		                                         projectDialog_.ShouldRestorePreviousProjectSession()
		                                     ? missingRecentProject_ : std::filesystem::path{};
		const auto previousRoot = preferences_.projectScenes.contains(candidate.GetRoot())
		                              ? std::filesystem::path{} : previousProjectRoot;
		const auto last = create ? std::filesystem::path{}
		                         : preferences_.LastProjectScene(candidate.GetRoot(), &unavailableLast,
		                                                         previousRoot);
		std::filesystem::path previousSceneLocation;
		if (!last.empty()) {
			if (const auto previous = preferences_.projectScenes.find(previousRoot);
			    previous != preferences_.projectScenes.end()) {
				previousSceneLocation = previousRoot / previous->second;
			}
		}
		const bool lastIsStartup = !last.empty() && !startup.empty() &&
		                           PathUtils::IsSameFile(last, startup);
		bool restored = false;
		std::string lastError;
		if (!last.empty() && !lastIsStartup) {
			restored = OpenScene(last, false, &config.lighting, previousSceneLocation);
			if (!restored) {
				unavailableLast = last;
				lastError = document_.GetError();
			}
		}
		std::string startupError;
		bool needsScene = false;

		if (!restored) {
			if (!startup.empty() &&
			    !OpenScene(startup, false, &config.lighting,
			               lastIsStartup ? previousSceneLocation : std::filesystem::path{})) {
				startupError = std::format(
				    "Could not open the project startup scene: {}\n{}\n"
				    "Open or save a scene in the project, then choose "
				    "File > Set Current Scene as Startup.",
				    PathUtils::ToUtf8(startup), document_.GetError());
			}

			if (startup.empty() || !startupError.empty()) {
				needsScene = true;
				pendingAction_ = EditorAction::NEW;
				ExecutePendingAction();

				if (stage_ != DialogStage::IDLE) {
					return;
				}
			}
		}

		document_.SetProjectRoot(candidate.GetRoot());
		project_ = std::move(candidate);
		exportDialog_.SwitchProject(project_.GetRoot());
		if (const auto found = preferences_.gameExportDrafts.find(project_.GetRoot());
		    found != preferences_.gameExportDrafts.end()) {
			exportDialog_.RestoreDraft(found->second);
		}
		failedRestoredScene_.clear();
		lastPlayScenePath_.clear();
		lastPlayFollowedEditingScene_ = false;
		lastPlayCameraOverride_ = 0;
		playFailureReplay_.reset();
		exportConfig_ = std::move(config);
		assetSources_ = std::move(sources);
		assets_ = std::move(assets);
		assetsPanel_.Clear();
		suspendedRecyclePaths_.clear();
		assetsPanel_.RestoreDirectory(
		    preferences_.LastProjectAssetFolder(project_.GetRoot(), previousProjectRoot), assets_);
		texturePreviewPanel_.Clear();
		stage_ = DialogStage::IDLE;

		const auto missing = std::exchange(missingRecentProject_, std::filesystem::path{});
		std::error_code status;
		const bool removedRecent =
		    !missing.empty() &&
		    std::filesystem::symlink_status(missing / L"project.json", status)
		            .type() == std::filesystem::file_type::not_found &&
		    std::erase(preferences_.recentProjects, missing) != 0;
		bool rememberedUnavailableScene = false;
		if (!unavailableLast.empty() &&
		    (startup.empty() || !PathUtils::IsSameFile(unavailableLast, startup))) {
			if (const auto previous = preferences_.projectScenes.find(previousRoot);
			    previous != preferences_.projectScenes.end()) {
				rememberedUnavailableScene = preferences_.projectScenes.try_emplace(
				    project_.GetRoot(), previous->second).second;
				if (rememberedUnavailableScene) {
					preferences_.CopySceneViewAndSelection(previousRoot / previous->second,
					                                       unavailableLast);
				}
			}
			failedRestoredScene_ = unavailableLast;
			auto message = "Could not restore the last edited scene: " +
			               PathUtils::ToUtf8(unavailableLast);
			if (!lastError.empty()) {
				message += "\n" + lastError;
			}
			message += needsScene
			               ? "\nOpened an untitled scene."
			               : "\nOpened the startup scene: " + PathUtils::ToUtf8(startup);
			Logger::Warning(message, unavailableLast);
			consolePanel_.Show();
		}
		SaveEditorSession(removedRecent || rememberedUnavailableScene);
		if (!startupError.empty()) {
			Logger::Warning(startupError, startup);
			consolePanel_.Show();
		}
		if (recovery_.IsReady()) {
			ShowRecovery(true);
		}
		if (!create && needsScene && stage_ == DialogStage::IDLE &&
		    !assets_.GetSceneTargets().empty()) {
			StartAction(EditorAction::OPEN);
		}
	} catch (const std::exception &error) {
		reportFailure(error.what());
	}
}

void EditorScene::SaveEditorSession(bool force,
                                    const std::filesystem::path &sceneCopy) {
	if (!canSavePreferences_) {
		return;
	}

	auto current = preferences_;
	const auto recent = recentScenes_.GetPaths();
	current.recentScenes.assign(recent.begin(), recent.end());

	if (!project_.GetRoot().empty()) {
		current.lastProject = project_.GetRoot();
		if (auto draft = exportDialog_.GetDraft();
		    draft && current.gameExportDrafts.contains(project_.GetRoot())) {
			current.gameExportDrafts.insert_or_assign(project_.GetRoot(), std::move(*draft));
		}
		current.RememberProject(project_.GetRoot());
		// 自動で開いた代替シーンで、修復待ちの復帰先を上書きしない。
		if (failedRestoredScene_.empty()) {
			current.RememberProjectScene(project_.GetRoot(), document_.GetFilePath());
		}
		current.RememberProjectAssetFolder(project_.GetRoot(),
		                                  assetsPanel_.GetOpenDirectory());
	}

	const auto view = cameraController_.GetViewState();
	const auto selection = selection_.GetIds();
	current.RememberSceneView(document_.GetFilePath(), view);
	current.RememberSceneSelection(document_.GetFilePath(), selection);
	if (!sceneCopy.empty()) {
		current.RememberSceneView(sceneCopy, view);
		current.RememberSceneSelection(sceneCopy, selection);
	}

	if ((force || !(current == preferences_)) && current.Save()) {
		preferences_ = std::move(current);
	}
}

bool EditorScene::OpenScene(const std::filesystem::path &path, bool reportError,
                            const LightingSettings *projectLighting,
                            const std::filesystem::path &previousLocation) {
	const auto previousPath = document_.GetFilePath();
	SaveEditorSession();
	const bool opened = document_.Open(
	    path, projectLighting ? projectLighting : &exportConfig_.lighting);

	if (!opened) {
		if (reportError) {
			ShowError("Could not open the scene. The current scene was kept. See "
			          "the log for details.");
		}
		return false;
	}

	if (!projectLighting && !failedRestoredScene_.empty() &&
	    project_.CanUseAsStartupScene(document_.GetFilePath())) {
		failedRestoredScene_.clear();
	}
	if (lastPlayScenePath_.empty()) {
		lastPlayCameraOverride_ = 0;
		lastPlayFollowedEditingScene_ = false;
	}
	if (playFailureReplay_ && playFailureReplay_->path.empty()) {
		playFailureReplay_.reset();
	}
	ClearRecovery();
	sceneCheckPanel_.OnSceneOpened(previousPath, document_.GetFilePath());
	clipboard_.DetachExternalReferences();
	frameRequest_ = FrameRequest::NONE;
	frameObject_ = 0;
	FinishEdit();
	if (!previousPath.empty() &&
	    PathUtils::IsSameFile(previousPath, document_.GetFilePath())) {
		// 同じシーンの再読み込みでは、設定の保存結果に関わらず現在の視点と選択を使う。
		hierarchyPanel_.Clear();
		selection_.RemoveMissing(document_.GetScene());
		if (const auto active = selection_.GetActiveId()) {
			hierarchyPanel_.RequestReveal(active);
		}
	} else {
		const auto &openedPath = document_.GetFilePath();
		// 場所を選び直したシーンに表示状態がなければ、移動前の記録を使う。
		const bool usePreviousLocation =
		    !previousLocation.empty() &&
		    (projectLighting || project_.CanUseAsStartupScene(openedPath)) &&
		    !preferences_.FindSceneView(openedPath) &&
		    preferences_.LastSceneSelection(openedPath).empty();
		RestoreSceneViewAndSelection(usePreviousLocation ? previousLocation : openedPath);
	}
	pendingPlayCamera_ = 0;
	playMode_.SetPreviewCamera(0);
	inspectorPanel_.Clear();
	recentScenes_.Add(document_.GetFilePath());
	stage_ = DialogStage::IDLE;
	return true;
}

void EditorScene::RestoreSceneViewAndSelection(const std::filesystem::path &source,
                                                const SceneRecovery::Snapshot *recovery) {
	selection_.Clear();
	cameraController_.Reset();
	hierarchyPanel_.Clear();

	const auto *view = recovery && recovery->view ? &*recovery->view
	                                              : preferences_.FindSceneView(source);
	if (view) {
		cameraController_.SetViewState(*view);
	}

	const auto lastSelection = recovery && recovery->selection
	                               ? std::span<const SceneObjectId>(*recovery->selection)
	                               : preferences_.LastSceneSelection(source);
	if (!lastSelection.empty()) {
		selection_.Set({lastSelection.begin(), lastSelection.end()});
		selection_.RemoveMissing(document_.GetScene());
		if (const auto active = selection_.GetActiveId()) {
			hierarchyPanel_.RequestReveal(active);
		}
	}
}

void EditorScene::ShowError(std::string message, SceneObjectId objectId,
                            std::filesystem::path scriptPath) {
	errorMessage_ = std::move(message);
	errorScriptPath_ = std::move(scriptPath);
	errorInputAction_.clear();
	errorLoadingPlayScene_ = false;
	errorScenePath_.clear();
	failedOpenScenePath_.clear();
	reloadBackupPath_.clear();
	sceneReloadFailed_ = false;
	failedOpenSceneObject_ = 0;
	errorReloadAssetPath_.clear();
	errorReplayPath_.reset();
	errorRestartCurrentScene_.reset();
	errorPlayLogId_ = 0;
	errorReplayCameraOverride_ = 0;
	errorActionMessage_.clear();
	failedAssetMoves_.clear();
	failedAssetMoveWasCutPaste_ = false;
	failedAssetCopy_ = {};
	sceneSaveFailed_ = false;
	sceneSaveConflict_ = false;
	errorObjectId_ = document_.GetScene().Contains(objectId) ? objectId : 0;
	errorOpenRequested_ = true;

	pendingAction_ = EditorAction::NONE;
	stage_ = DialogStage::ERROR;
}

void EditorScene::ShowSceneSaveError() {
	const auto action = pendingAction_;
	if (action == EditorAction::WRITE_GAME_EXPORT) {
		exportDialog_.PreserveDraft();
	}
	ShowError(document_.GetError());
	sceneSaveFailed_ = true;
	sceneSaveConflict_ = document_.HasSaveConflict();
	pendingAction_ = action;
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
			    !ColliderUtils::GetWorldBounds(*object->collider,
			                                   document_.GetScene(),
			                                   request.objectId, bounds)) {
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

			if (!document_.GetHistory().SetWorldPosition(
			        request.objectId, object->respawn->position)) {
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
				ShowError("Could not copy an invalid Collider.");
			}

			return;
		}

		if (request.action == InspectorPanel::ObjectAction::PASTE_COLLIDER) {
			const auto *copied = clipboard_.GetCollider();

			if (!copied || !document_.GetHistory().SetCollider(request.objectId,
			                                                   *copied)) {
				ShowError("Could not paste the Collider. Copy a valid "
				          "shape and check the target Transform.");
			}

			return;
		}

		if (request.action == InspectorPanel::ObjectAction::COPY_TRANSFORM) {
			SceneWorldTransform world;
			if (!SceneHierarchy::GetWorldTransform(document_.GetScene(),
			                                       request.objectId, world) ||
			    !clipboard_.CopyTransform(object->transform,
			                              world.GetPosition())) {
				ShowError("Could not copy an invalid Transform.");
			}

			return;
		}

		if (request.action ==
		    InspectorPanel::ObjectAction::PASTE_RESPAWN_POSITION) {
			const auto *copied = clipboard_.GetWorldPosition();

			if (!object->respawn || !copied) {
				ShowError("Copy a Transform and inspect an object with Respawn "
				          "before pasting its position.");
				return;
			}

			auto settings = *object->respawn;
			settings.useStartPosition = false;
			settings.position = *copied;

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
			    respawn
			        ? RespawnPreview::GetFrameBounds(document_.GetScene(),
			                                         request.objectId, corners)
			        : ColliderBounds::GetFrameBounds(document_.GetScene(),
			                                         request.objectId, corners);

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
				inspected = CollisionWorld::FindColliderOverlaps(
				    document_.GetScene(), request.objectId, objects);
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
			SceneWorldTransform transform;
			std::vector<SceneObjectId> ignored;

			if (!object->character || !object->collider ||
			    !SceneHierarchy::GetWorldTransform(
			        document_.GetScene(), request.objectId, transform) ||
			    !SceneHierarchy::GetSubtree(document_.GetScene(),
			                                request.objectId, ignored) ||
			    !world.Rebuild(document_.GetScene(),
			                   std::span<const SceneObjectId>{ignored}) ||
			    !controller.Initialize(transform.matrix, *object->collider,
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

void EditorScene::ReloadAsset(const std::filesystem::path &path, bool texture) {
	std::string error;
	try {
		FinishEdit();
		const bool reloaded = texture ? !!materials_.ReloadTexture(path)
		                              : !!models_.Reload(path);
		if (reloaded && texture) {
			texturePreviewPanel_.RequestReload(path);
		}
		if (reloaded &&
		    (!texture || (renderer_.ReloadCanvasImage(path, error) &&
		                  playMode_.ReloadCanvasImage(path, error)))) {
			sceneCheckPanel_.RequestRecheckIfOpen();
			return;
		}
		if (!reloaded) {
			error = "See the log for details.";
		}
	} catch (const std::exception &failure) {
		error = failure.what();
		Logger::Error(error, path);
	}
	ShowError(std::format("Could not reload {}:\n{}\n{}", texture ? "texture" : "model",
	                      PathUtils::ToUtf8(path), error));
	errorReloadAssetPath_ = path;
	errorReloadIsTexture_ = texture;
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

		auto &history = document_.GetHistory();
		const auto previousState = history.GetCurrentStateId();
		if (!history.SetModelPaths(selection_.GetIds(), modelPath)) {
			ShowError("Could not replace the meshes. Every selected object "
			          "must have a Mesh component.");
			return false;
		}
		if (history.GetCurrentStateId() != previousState) {
			sceneCheckPanel_.RequestRecheckIfOpen();
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
		if (!SceneActivity::IsActive(scene, id) ||
		    std::find(excluded.begin(), excluded.end(), id) != excluded.end()) {
			continue;
		}

		std::array<DirectX::XMFLOAT3, 8> corners;
		const bool missingModel = renderer.IsModelUnavailable(id);
		const bool boundsReady =
		    (!object.HasMesh() || !object.meshRenderer || missingModel)
		        ? showObjectMarkers_ &&
		              ObjectMarkers::GetBounds(scene, id, corners, missingModel)
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
			            ? RespawnPreview::GetFrameBounds(scene, frameObject_,
			                                             corners)
			            : ColliderBounds::GetFrameBounds(scene, frameObject_,
			                                             corners);
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
		if ((!selectedOnly && !SceneActivity::IsActive(scene, id)) ||
		    (object.IsHidden() && object.HasMesh() && !includeHidden) ||
		    (selectedOnly && !selection.Contains(id))) {
			continue;
		}

		std::array<DirectX::XMFLOAT3, 8> corners;

		const bool missingModel = renderer.IsModelUnavailable(id);
		bool boundsReady =
		    (!object.HasMesh() || !object.meshRenderer || missingModel)
		        ? ObjectFrameBounds::Get(scene, id, corners, missingModel)
		        : renderer.GetObjectBounds(id, corners);
		// 非表示・読み込み不能なモデルも、検証結果や再生状態からはColliderか原点へ寄せられる。
		if (!boundsReady &&
		    (includeHidden || !SceneActivity::IsActive(scene, id))) {
			boundsReady = ObjectFrameBounds::Get(scene, id, corners, true);
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
	SceneWorldTransform activeWorld;
	const bool hasActive = SceneHierarchy::GetWorldTransform(
	    runtime, playSelection_.GetActiveId(), activeWorld);
	const auto activePosition = activeWorld.GetPosition();
	UpdatePlaySelectionFollow();
	cameraController_.Update(input_, viewport, deltaTime,
	                         hasActive ? &activePosition : nullptr);
	auto *target = sceneViewPanel_.GetRenderTexture();

	if (!target) {
		return true;
	}

	std::vector<ParticleDrawBatch> particles;
	std::string particleError;

	if (!playMode_.GetSession().GetParticles(particles, particleError)) {
		Logger::Error(particleError);
		return false;
	}

	playSceneRenderer_.SetParticles(std::move(particles));
	std::vector<AnimatorDrawPose> poses;

	if (!playMode_.GetSession().GetAnimatorPoses(poses, particleError) ||
	    !playSceneRenderer_.SetAnimatorPoses(std::move(poses))) {
		Logger::Error(particleError);
		return false;
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
	if (!playUsesEditingScene_ || !playMode_.GetSession().IsInitialScene())
		return;
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
	if (!playUsesEditingScene_ || !playMode_.GetSession().IsInitialScene())
		return;
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
	SceneWorldTransform world;

	if (!followPlaySelection_ || !playMode_.IsPlaying() ||
	    !SceneHierarchy::GetWorldTransform(playMode_.GetSession().GetScene(),
	                                       id, world)) {
		followPlaySelection_ = false;
		followedPlayObject_ = 0;
		return;
	}

	const auto position = world.GetPosition();

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

void EditorScene::SynchronizePlayScene() {
	if (!playMode_.IsPlaying()) {
		playSceneRevision_ = 0;
		return;
	}

	const auto revision = playMode_.GetSession().GetSceneRevision();

	if (playSceneRevision_ != 0 && playSceneRevision_ != revision) {
		const auto *loaded = playMode_.GetSession().GetLoadedScenePath();
		lastPlayScenePath_ = loaded ? *loaded : playInitialScenePath_;
		lastPlayFollowedEditingScene_ = !loaded && playUsesEditingScene_;
		lastPlayCameraOverride_ = loaded ? 0 : playInitialCameraOverride_;
		ClearPlaySelection();
		playSelectionActive_ = true;
	}

	playSceneRevision_ = revision;
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
			          "valid Colliders and representable coordinates. No "
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
			ShowError("Could not paste all Colliders. Check the selected "
			          "shapes and Transforms. No colliders were changed.");
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorScene::PasteCollidersToSelection: {}",
		                          error.what()));
		ShowError("Could not paste the Colliders. See the log for details.");
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
